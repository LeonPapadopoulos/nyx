#include "NyxPCH.h"
#include "VulkanPainter.h"

#include "Paths.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	namespace
	{
		// 8 bits per channel, like the swapchain: the painter blends in the same sRGB-encoded
		// values ImGui does, so the two match side by side.
		constexpr vk::Format TargetFormat = vk::Format::eR8G8B8A8Unorm;

		// How many targets and segment sets can exist at once; each takes two descriptor sets.
		constexpr uint32_t MaxTargets = 32;

		// The binding of each buffer in Painter.frag
		constexpr uint32_t CommandsBinding = 0;
		constexpr uint32_t GroupsBinding = 1;
		constexpr uint32_t TilesBinding = 2;
		constexpr uint32_t EntriesBinding = 3;
		constexpr uint32_t BindingCount = 4;

		// The binding of each buffer in PainterBin.comp
		constexpr uint32_t BinSegmentsBinding = 0;
		constexpr uint32_t BinCommandsBinding = 1;
		constexpr uint32_t BinTilesBinding = 2;
		constexpr uint32_t BinEntriesBinding = 3;
		constexpr uint32_t BinFirstKeptBinding = 4;
		constexpr uint32_t BinSumsBinding = 5;
		constexpr uint32_t BinBindingCount = 6;

		// PainterBinPushConstants in PainterBin.comp
		struct PainterBinPushConstants
		{
			uint32_t TileCount = 0;
			uint32_t SegmentCount = 0;
			uint32_t EntryCapacity = 0;
			uint32_t Pass = 0;
		};

		// The sums buffer starts with the next free entry, padded to one PainterBinSums, then
		// holds one PainterBinSums per segment.
		constexpr vk::DeviceSize SumsHeaderSize = sizeof(PainterBinSums);

		// PainterPushConstants in Painter.vert and Painter.frag
		struct PainterPushConstants
		{
			// Where the drawn part starts in each buffer: a segment's offsets into its frame's
			// buffers, or 0 for a canvas, whose buffers hold only its own frame
			uint32_t CommandBase = 0;
			uint32_t GroupBase = 0;
			uint32_t TileBase = 0;
			uint32_t EntryBase = 0;
			// The tile grid: the target pixel its top left tile starts at, and its size in tiles
			uint32_t OriginX = 0;
			uint32_t OriginY = 0;
			uint32_t TilesX = 0;
			uint32_t TilesY = 0;
			// The target's size in pixels
			float TargetWidth = 0.0f;
			float TargetHeight = 0.0f;
			uint32_t DebugView = 0;
			uint32_t Unused = 0;
		};

		static_assert(sizeof(PainterPushConstants) == 48, "The shaders read exactly 48 bytes of push constants");

		// Painter.vert draws the grid's quad as two triangles.
		constexpr uint32_t QuadVertexCount = 6;

		// Buffers start at this size, so that an empty frame still has valid buffers (Vulkan has
		// no empty ones), and grow by doubling.
		constexpr vk::DeviceSize MinBufferSize = 4096;

	}

	void VulkanPainterBuffers::Release()
	{
		DrawSet = nullptr;
		BinSet = nullptr;
		for (GpuBuffer* buffer : { &CommandsUpload, &GroupsUpload, &BinCommandsUpload, &BinSegmentsUpload, &Commands, &Groups, &BinCommands, &BinSegments, &Tiles,
				 &Entries, &FirstKept, &Sums, &SumsReadBack, &TilesReadBack, &EntriesReadBack })
		{
			buffer->Mapped = nullptr;
			buffer->Buffer = nullptr;
			buffer->Memory = nullptr;
			buffer->Size = 0;
		}
	}

	void VulkanPainterTarget::Release()
	{
		Segments.Release();
		Framebuffer = nullptr;
		ImageView = nullptr;
		Image = nullptr;
		ImageMemory = nullptr;
		Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanPainterSegments::Clear(const PainterBudget& budget)
	{
		Budget = budget;
		Commands.clear();
		Groups.clear();
		BinCommands.clear();
		BinSegments.clear();
		TileCount = 0;
		MaxEntries = 0;
		Painters.clear();
		Segments.clear();
		Problems.clear();
		// What was read back belonged to the segments that just went.
		ReadBackProblems.clear();
		ReadTiles.clear();
		ReadEntries.clear();
	}

	uint32_t VulkanPainterSegments::Add(const Painter& painter, const PainterSegment& segment, bool bWholeTarget)
	{
		const uint32_t painterIndex = FindOrAddPainter(painter);
		MeasurePainterSegment(painter, segment, bWholeTarget, Budget, Measured);

		SegmentPart part;
		part.Painter = painterIndex;
		// Tile entries count from the segment's first command.
		part.CommandBase = Painters[painterIndex].CommandBase + segment.FirstCommand;
		// Each segment's tiles start at a whole workgroup of the binner, so that a workgroup sums
		// one segment's tiles.
		part.FirstTile = (TileCount + PainterBinWorkgroupSize - 1) / PainterBinWorkgroupSize * PainterBinWorkgroupSize;
		part.OriginX = Measured.OriginX;
		part.OriginY = Measured.OriginY;
		part.TilesX = Measured.TilesX;
		part.TilesY = Measured.TilesY;
		part.Stats = Measured.Stats;
		TileCount = part.FirstTile + part.TilesX * part.TilesY;
		MaxEntries += Measured.MaxEntries;

		PainterBinSegment binSegment;
		binSegment.FirstCommand = static_cast<uint32_t>(BinCommands.size());
		binSegment.CommandCount = static_cast<uint32_t>(Measured.Commands.size());
		binSegment.FirstTile = part.FirstTile;
		binSegment.TilesX = part.TilesX;
		binSegment.TilesY = part.TilesY;
		binSegment.OriginX = part.OriginX;
		binSegment.OriginY = part.OriginY;
		BinCommands.insert(BinCommands.end(), Measured.Commands.begin(), Measured.Commands.end());
		BinSegments.push_back(binSegment);
		Problems.insert(Problems.end(), Measured.Problems.begin(), Measured.Problems.end());

		Segments.push_back(part);
		return static_cast<uint32_t>(Segments.size() - 1);
	}

	void VulkanPainterSegments::ReadBack()
	{
		if (!bReadBackPending)
		{
			return;
		}
		bReadBackPending = false;

		// The next free entry, then one sum per segment; entries past the capacity weren't written.
		const auto* header = static_cast<const uint32_t*>(Buffers.SumsReadBack.Mapped);
		const auto* sums = static_cast<const PainterBinSums*>(Buffers.SumsReadBack.Mapped) + 1;
		uint32_t overflowed = 0;
		for (size_t index = 0; index < Segments.size(); ++index)
		{
			PainterBinningStats& stats = Segments[index].Stats;
			stats.TilesInUse = sums[index].TilesInUse;
			stats.TileEntries = sums[index].Entries;
			stats.MaxCommandsPerTile = sums[index].MaxPerTile;
			stats.EntriesBeforeReduction = sums[index].EntriesBefore;
			overflowed += sums[index].Overflowed != 0 ? 1 : 0;
		}
		if (overflowed > 0)
		{
			ReadBackProblems.push_back("The painter's budget ran out: " + std::to_string(overflowed) + " segments had tiles whose entries didn't fit and were drawn empty (at most " +
				std::to_string(Budget.MaxTileEntries) + " tile entries per frame)");
		}

		if (bTilesReadBackPending)
		{
			const auto* tiles = static_cast<const PainterTile*>(Buffers.TilesReadBack.Mapped);
			const auto* entries = static_cast<const uint32_t*>(Buffers.EntriesReadBack.Mapped);
			ReadTiles.assign(tiles, tiles + TileCount);
			ReadEntries.assign(entries, entries + std::min(header[0], EntryCapacity));
			bTilesReadBackPending = false;
		}
	}

	PainterTiles VulkanPainterSegments::GetBinned(uint32_t segment) const
	{
		PainterTiles binned;
		if (segment >= Segments.size())
		{
			return binned;
		}
		const SegmentPart& part = Segments[segment];
		binned.OriginX = part.OriginX;
		binned.OriginY = part.OriginY;
		binned.TilesX = part.TilesX;
		binned.TilesY = part.TilesY;
		binned.Stats = part.Stats;
		binned.Problems = Problems;
		binned.Problems.insert(binned.Problems.end(), ReadBackProblems.begin(), ReadBackProblems.end());

		const size_t tileCount = static_cast<size_t>(part.TilesX) * part.TilesY;
		if (ReadTiles.size() < part.FirstTile + tileCount)
		{
			return binned;
		}
		// Each tile's entries, now counted from the segment's own first entry
		binned.Tiles.assign(ReadTiles.begin() + part.FirstTile, ReadTiles.begin() + part.FirstTile + tileCount);
		for (PainterTile& tile : binned.Tiles)
		{
			const uint32_t first = static_cast<uint32_t>(binned.Entries.size());
			if (tile.FirstEntry + tile.Count <= ReadEntries.size())
			{
				binned.Entries.insert(binned.Entries.end(), ReadEntries.begin() + tile.FirstEntry, ReadEntries.begin() + tile.FirstEntry + tile.Count);
			}
			tile.FirstEntry = first;
		}
		return binned;
	}

	void VulkanPainterSegments::AddHeat(uint32_t segment, PainterHeatGrid& grid) const
	{
		if (segment >= Segments.size())
		{
			return;
		}
		// The segment's grid starts at a whole tile of the target.
		const SegmentPart& part = Segments[segment];
		if (ReadTiles.size() < part.FirstTile + static_cast<size_t>(part.TilesX) * part.TilesY)
		{
			return;
		}
		const uint32_t firstX = part.OriginX / PainterTileSize;
		const uint32_t firstY = part.OriginY / PainterTileSize;
		for (uint32_t tileY = 0; tileY < part.TilesY; ++tileY)
		{
			for (uint32_t tileX = 0; tileX < part.TilesX; ++tileX)
			{
				grid.Add(firstX + tileX, firstY + tileY, ReadTiles[part.FirstTile + tileY * part.TilesX + tileX].Count);
			}
		}
	}

	PainterReduction VulkanPainterSegments::GetReduction() const
	{
		PainterReduction reduction;
		for (const SegmentPart& part : Segments)
		{
			reduction.EntriesBefore += part.Stats.EntriesBeforeReduction;
			reduction.Kept += part.Stats.TileEntries;
		}
		return reduction;
	}

	size_t VulkanPainterSegments::GetUploadBytes() const
	{
		return Commands.size() * sizeof(PainterCommand) + Groups.size() * sizeof(PainterGroup) + BinCommands.size() * sizeof(PainterBinCommand) +
			BinSegments.size() * sizeof(PainterBinSegment);
	}

	uint32_t VulkanPainterSegments::FindOrAddPainter(const Painter& painter)
	{
		for (uint32_t index = 0; index < Painters.size(); ++index)
		{
			if (Painters[index].Source == &painter)
			{
				return index;
			}
		}

		// The whole frame of the painter goes in once; its segments are ranges of it, and its
		// commands' group indices count from its first group.
		PainterPart part;
		part.Source = &painter;
		part.CommandBase = static_cast<uint32_t>(Commands.size());
		part.GroupBase = static_cast<uint32_t>(Groups.size());
		part.Textures = painter.GetTextures();
		Commands.insert(Commands.end(), painter.GetCommands().begin(), painter.GetCommands().end());
		Groups.insert(Groups.end(), painter.GetGroups().begin(), painter.GetGroups().end());

		Painters.push_back(std::move(part));
		return static_cast<uint32_t>(Painters.size() - 1);
	}

	void VulkanPainterSegments::Release()
	{
		Clear();
		Buffers.Release();
	}

	void VulkanPainter::Initialize(Nyx::VulkanContext& context, ShaderPrograms& programs)
	{
		Context = &context;
		Programs = &programs;
		CheckDeviceLimits();
		CreateRenderPass();
		CreateDescriptorSetLayouts();
		CreateDescriptorPool();
		CreatePipelineLayout();
		CreateBinPipeline();
		CreateEmptyTexture();
		CanvasPipeline = CreatePipeline(*RenderPass);
	}

	void VulkanPainter::RebuildPipelines()
	{
		CreateBinPipeline();
		CanvasPipeline = CreatePipeline(*RenderPass);
		for (SegmentPipeline& segmentPipeline : SegmentPipelines)
		{
			segmentPipeline.Pipeline = CreatePipeline(*segmentPipeline.RenderPass);
		}
	}

	void VulkanPainter::Shutdown()
	{
		EmptyTextureSet = nullptr;
		EmptySampler = nullptr;
		EmptyImageView = nullptr;
		EmptyImage = nullptr;
		EmptyImageMemory = nullptr;

		SegmentPipelines.clear();
		CanvasPipeline = nullptr;
		BinPipeline = nullptr;
		BinPipelineLayout = nullptr;
		BinSetLayout = nullptr;
		PipelineLayout = nullptr;
		DescriptorPool = nullptr;
		TextureSetLayout = nullptr;
		DescriptorSetLayout = nullptr;
		RenderPass = nullptr;
		Context = nullptr;
	}

	void VulkanPainter::Draw(vk::raii::CommandBuffer& commandBuffer, const Painter& painter, VulkanPainterTarget& target, const PainterDrawOptions& options)
	{
		// The last draw is done by now (the renderer waits for the frame before; the tests wait for
		// each draw): what it read back is taken before this frame starts over.
		VulkanPainterSegments& segments = target.Segments;
		segments.ReadBack();
		segments.Clear(options.Budget);
		const uint32_t segment = segments.Add(painter, PainterSegment{ 0, static_cast<uint32_t>(painter.GetCommands().size()) }, true);

		const vk::Extent2D extent{ painter.GetTargetWidth(), painter.GetTargetHeight() };
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}

		PrepareImage(target, extent);
		Bin(*commandBuffer, segments);
		const VulkanPainterSegments::SegmentPart& part = segments.Segments[segment];

		// The clear colour is straight, like every colour given to the painter; the image holds
		// premultiplied colour, like everything the shader writes.
		const glm::vec4 clear = options.ClearColor;
		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a });

		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = *RenderPass;
		beginInfo.framebuffer = *target.Framebuffer;
		beginInfo.renderArea = vk::Rect2D({ 0, 0 }, extent);
		beginInfo.clearValueCount = 1;
		beginInfo.pClearValues = &clearValue;
		commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D({ 0, 0 }, extent));

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *CanvasPipeline);
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 0, { *segments.Buffers.DrawSet }, {});
		BindTextures(*commandBuffer, painter.GetTextures());

		// The grid covers the whole target, and the buffers hold only this frame.
		PainterPushConstants push;
		push.TileBase = part.FirstTile;
		push.TilesX = part.TilesX;
		push.TilesY = part.TilesY;
		push.TargetWidth = static_cast<float>(extent.width);
		push.TargetHeight = static_cast<float>(extent.height);
		push.DebugView = static_cast<uint32_t>(options.DebugView);
		commandBuffer.pushConstants<PainterPushConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { push });

		commandBuffer.draw(QuadVertexCount, 1, 0, 0);

		commandBuffer.endRenderPass();
	}

	void VulkanPainter::Bin(vk::CommandBuffer commandBuffer, VulkanPainterSegments& segments)
	{
		if (segments.Segments.empty())
		{
			segments.bReadBackPending = false;
			return;
		}
		PrepareBuffers(segments);
		VulkanPainterBuffers& buffers = segments.Buffers;

		// What the CPU wrote, into the GPU's own buffers; the sums start at 0.
		const auto copy = [&](const VulkanPainterBuffers::GpuBuffer& from, const VulkanPainterBuffers::GpuBuffer& to, vk::DeviceSize size)
		{
			if (size > 0)
			{
				commandBuffer.copyBuffer(*from.Buffer, *to.Buffer, vk::BufferCopy(0, 0, size));
			}
		};
		copy(buffers.CommandsUpload, buffers.Commands, segments.Commands.size() * sizeof(PainterCommand));
		copy(buffers.GroupsUpload, buffers.Groups, segments.Groups.size() * sizeof(PainterGroup));
		copy(buffers.BinCommandsUpload, buffers.BinCommands, segments.BinCommands.size() * sizeof(PainterBinCommand));
		copy(buffers.BinSegmentsUpload, buffers.BinSegments, segments.BinSegments.size() * sizeof(PainterBinSegment));
		const vk::DeviceSize sumsSize = SumsHeaderSize + segments.Segments.size() * sizeof(PainterBinSums);
		commandBuffer.fillBuffer(*buffers.Sums.Buffer, 0, sumsSize, 0);

		// The copies before the binner and the painter shader read them, and the last drawing's
		// reads of the tiles and entries before the binner writes them again
		const auto barrier = [&](vk::PipelineStageFlags fromStages, vk::AccessFlags fromAccess, vk::PipelineStageFlags toStages, vk::AccessFlags toAccess)
		{
			const vk::MemoryBarrier memoryBarrier(fromAccess, toAccess);
			commandBuffer.pipelineBarrier(fromStages, toStages, {}, memoryBarrier, {}, {});
		};
		barrier(vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eTransferWrite,
			vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

		PainterBinPushConstants push;
		push.TileCount = segments.TileCount;
		push.SegmentCount = static_cast<uint32_t>(segments.Segments.size());
		push.EntryCapacity = segments.EntryCapacity;
		const uint32_t workgroups = (segments.TileCount + PainterBinWorkgroupSize - 1) / PainterBinWorkgroupSize;
		if (workgroups > 0)
		{
			commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *BinPipeline);
			commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *BinPipelineLayout, 0, { *buffers.BinSet }, {});

			// Pass 0 counts and places each tile's entries; pass 1 writes them.
			push.Pass = 0;
			commandBuffer.pushConstants<PainterBinPushConstants>(*BinPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, push);
			commandBuffer.dispatch(workgroups, 1, 1);
			barrier(vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			push.Pass = 1;
			commandBuffer.pushConstants<PainterBinPushConstants>(*BinPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, push);
			commandBuffer.dispatch(workgroups, 1, 1);
		}

		// The tiles and entries before the painter shader reads them, here or in a later command
		// buffer on the queue, and before they are copied back
		barrier(vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead);

		// Read back once the GPU is done: the sums, and while asked for the tiles and entries
		copy(buffers.Sums, buffers.SumsReadBack, sumsSize);
		if (segments.bReadBackTiles)
		{
			copy(buffers.Tiles, buffers.TilesReadBack, static_cast<vk::DeviceSize>(segments.TileCount) * sizeof(PainterTile));
			copy(buffers.Entries, buffers.EntriesReadBack, static_cast<vk::DeviceSize>(segments.EntryCapacity) * sizeof(uint32_t));
		}
		barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostRead);
		segments.bReadBackPending = true;
		segments.bTilesReadBackPending = segments.bReadBackTiles;
	}

	void VulkanPainter::DrawSegment(vk::CommandBuffer commandBuffer, const VulkanPainterSegments& segments, uint32_t segment, vk::Format colorFormat, vk::Extent2D framebufferExtent)
	{
		if (segment >= segments.Segments.size() || !*segments.Buffers.DrawSet)
		{
			return;
		}
		const VulkanPainterSegments::SegmentPart& part = segments.Segments[segment];
		const VulkanPainterSegments::PainterPart& painter = segments.Painters[part.Painter];

		// Only the grid's pixels, as far as the framebuffer reaches. Whoever drew before may have
		// left any scissor set (ImGui leaves its last clip rect), so it is set here.
		const uint32_t gridMaxX = std::min(part.OriginX + part.TilesX * PainterTileSize, framebufferExtent.width);
		const uint32_t gridMaxY = std::min(part.OriginY + part.TilesY * PainterTileSize, framebufferExtent.height);
		if (gridMaxX <= part.OriginX || gridMaxY <= part.OriginY)
		{
			// No tiles (the segment is clipped away), or none on the framebuffer
			return;
		}
		const vk::Rect2D scissor({ static_cast<int32_t>(part.OriginX), static_cast<int32_t>(part.OriginY) }, { gridMaxX - part.OriginX, gridMaxY - part.OriginY });

		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(framebufferExtent.width), static_cast<float>(framebufferExtent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, scissor);

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *GetSegmentPipeline(colorFormat));
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 0, { *segments.Buffers.DrawSet }, {});
		BindTextures(commandBuffer, painter.Textures);

		// The tiles' first entries count from the start of the entry buffer.
		PainterPushConstants push;
		push.CommandBase = part.CommandBase;
		push.GroupBase = painter.GroupBase;
		push.TileBase = part.FirstTile;
		push.EntryBase = 0;
		push.OriginX = part.OriginX;
		push.OriginY = part.OriginY;
		push.TilesX = part.TilesX;
		push.TilesY = part.TilesY;
		push.TargetWidth = static_cast<float>(framebufferExtent.width);
		push.TargetHeight = static_cast<float>(framebufferExtent.height);
		commandBuffer.pushConstants<PainterPushConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);

		commandBuffer.draw(QuadVertexCount, 1, 0, 0);
	}

	void VulkanPainter::CreateRenderPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = TargetFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		// Ready to be shown, e.g. as an ImGui image
		colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		std::array<vk::SubpassDependency, 2> dependencies{};
		// Earlier reads of the image (last frame's ImGui) finish before it is cleared and painted.
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = {};
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// The painted image is read afterwards, by a shader (ImGui) or a copy (the GPU tests).
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		RenderPass = vk::raii::RenderPass(Context->GetDevice(), renderPassInfo);
	}

	void VulkanPainter::CheckDeviceLimits()
	{
		// The commands' set plus one set per texture (PainterCommands.h, PainterMaxTextures)
		const uint32_t setsNeeded = 1 + PainterMaxTextures;
		const uint32_t setsAvailable = Context->GetPhysicalDevice().getProperties().limits.maxBoundDescriptorSets;
		if (setsAvailable < setsNeeded)
		{
			throw std::runtime_error("Painter: the GPU binds " + std::to_string(setsAvailable) + " descriptor sets at once, the painter needs " +
				std::to_string(setsNeeded) + "; lower PainterMaxTextures");
		}
	}

	void VulkanPainter::CreateDescriptorSetLayouts()
	{
		std::array<vk::DescriptorSetLayoutBinding, BindingCount> bindings{};
		for (uint32_t binding = 0; binding < BindingCount; ++binding)
		{
			bindings[binding].binding = binding;
			bindings[binding].descriptorType = vk::DescriptorType::eStorageBuffer;
			bindings[binding].descriptorCount = 1;
			bindings[binding].stageFlags = vk::ShaderStageFlagBits::eFragment;
		}

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		DescriptorSetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), layoutInfo);

		// The binner's: six storage buffers for the compute shader
		std::array<vk::DescriptorSetLayoutBinding, BinBindingCount> binBindings{};
		for (uint32_t binding = 0; binding < BinBindingCount; ++binding)
		{
			binBindings[binding].binding = binding;
			binBindings[binding].descriptorType = vk::DescriptorType::eStorageBuffer;
			binBindings[binding].descriptorCount = 1;
			binBindings[binding].stageFlags = vk::ShaderStageFlagBits::eCompute;
		}
		vk::DescriptorSetLayoutCreateInfo binLayoutInfo{};
		binLayoutInfo.bindingCount = static_cast<uint32_t>(binBindings.size());
		binLayoutInfo.pBindings = binBindings.data();
		BinSetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), binLayoutInfo);

		// Exactly as ImGui defines its texture sets, so that its sets are compatible with this one.
		vk::DescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 0;
		textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureBinding.descriptorCount = 1;
		textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo textureLayoutInfo{};
		textureLayoutInfo.bindingCount = 1;
		textureLayoutInfo.pBindings = &textureBinding;
		TextureSetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), textureLayoutInfo);
	}

	void VulkanPainter::CreateDescriptorPool()
	{
		// Two sets per target and segment set (drawing and binning), plus the empty texture's set.
		// All other textures come with sets of their own, made by ImGui.
		constexpr uint32_t TextureSets = 1;
		const std::array<vk::DescriptorPoolSize, 2> poolSizes = { {
			{ vk::DescriptorType::eStorageBuffer, MaxTargets * (BindingCount + BinBindingCount) },
			{ vk::DescriptorType::eCombinedImageSampler, TextureSets },
		} };

		vk::DescriptorPoolCreateInfo poolInfo{};
		// Targets and segment sets free their sets when they are released.
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = MaxTargets * 2 + TextureSets;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		DescriptorPool = vk::raii::DescriptorPool(Context->GetDevice(), poolInfo);
	}

	void VulkanPainter::CreateEmptyTexture()
	{
		vk::raii::Device& device = Context->GetDevice();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(1, 1, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = TargetFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		EmptyImage = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = EmptyImage.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		EmptyImageMemory = vk::raii::DeviceMemory(device, allocInfo);
		EmptyImage.bindMemory(*EmptyImageMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *EmptyImage;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = TargetFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		EmptyImageView = vk::raii::ImageView(device, viewInfo);

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eNearest;
		samplerInfo.minFilter = vk::Filter::eNearest;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		EmptySampler = vk::raii::Sampler(device, samplerInfo);

		vk::DescriptorSetAllocateInfo setInfo{};
		setInfo.descriptorPool = *DescriptorPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &*TextureSetLayout;
		EmptyTextureSet = std::move(vk::raii::DescriptorSets(device, setInfo).front());

		vk::DescriptorImageInfo imageDescriptor{ *EmptySampler, *EmptyImageView, vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::WriteDescriptorSet write{};
		write.dstSet = *EmptyTextureSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &imageDescriptor;
		device.updateDescriptorSets(write, {});

		// Cleared to transparent and made readable now, in a short submission of its own: segments
		// are drawn inside render passes, where an image can't be cleared.
		vk::CommandBufferAllocateInfo commandBufferInfo{};
		commandBufferInfo.commandPool = *Context->GetGraphicsCommandPool();
		commandBufferInfo.level = vk::CommandBufferLevel::ePrimary;
		commandBufferInfo.commandBufferCount = 1;
		vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, commandBufferInfo).front());
		commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		TransitionImageLayout(*commandBuffer, *EmptyImage, wholeImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eTopOfPipe, {}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

		commandBuffer.clearColorImage(*EmptyImage, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }), wholeImage);

		TransitionImageLayout(*commandBuffer, *EmptyImage, wholeImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		commandBuffer.end();
		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commandBuffer;
		Context->GetGraphicsQueue().submit(submitInfo);
		Context->GetGraphicsQueue().waitIdle();
	}

	void VulkanPainter::BindTextures(vk::CommandBuffer commandBuffer, const std::vector<ImTextureRef>& textures)
	{
		std::array<vk::DescriptorSet, PainterMaxTextures> textureSets;
		textureSets.fill(*EmptyTextureSet);

		for (size_t slot = 0; slot < textures.size() && slot < textureSets.size(); ++slot)
		{
			// With ImGui's Vulkan backend, a texture's ImTextureID is its descriptor set. The font
			// atlas has none until ImGui first uploads it (ImTextureID_Invalid, -1 since ImGui
			// 1.92.7), and 0 would be a null set; either way the glyphs or image stay invisible.
			const ImTextureID textureId = textures[slot].GetTexID();
			if (textureId != ImTextureID_Invalid && textureId != 0)
			{
				textureSets[slot] = vk::DescriptorSet(reinterpret_cast<VkDescriptorSet>(textureId));
			}
		}

		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 1, textureSets, {});
	}

	void VulkanPainter::CreatePipelineLayout()
	{
		// Both shaders read the push constants: the vertex shader the grid, the fragment shader
		// everything else.
		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PainterPushConstants);

		// Set 0 holds the commands; sets 1 to PainterMaxTextures one texture each.
		std::array<vk::DescriptorSetLayout, 1 + PainterMaxTextures> setLayouts;
		setLayouts.fill(*TextureSetLayout);
		setLayouts[0] = *DescriptorSetLayout;

		vk::PipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstantRange;
		PipelineLayout = vk::raii::PipelineLayout(Context->GetDevice(), layoutInfo);
	}

	void VulkanPainter::CreateBinPipeline()
	{
		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PainterBinPushConstants);

		vk::PipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &*BinSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstantRange;
		BinPipelineLayout = vk::raii::PipelineLayout(Context->GetDevice(), layoutInfo);

		const std::vector<uint32_t>& code = Programs->GetSpirv("PainterBin.comp");
		vk::ShaderModuleCreateInfo moduleInfo{};
		moduleInfo.codeSize = code.size() * sizeof(uint32_t);
		moduleInfo.pCode = code.data();
		const vk::raii::ShaderModule module(Context->GetDevice(), moduleInfo);

		vk::ComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
		pipelineInfo.stage.module = *module;
		pipelineInfo.stage.pName = "main";
		pipelineInfo.layout = *BinPipelineLayout;
		BinPipeline = vk::raii::Pipeline(Context->GetDevice(), Context->GetPipelineCache(), pipelineInfo);
	}

	const vk::raii::Pipeline& VulkanPainter::GetSegmentPipeline(vk::Format colorFormat)
	{
		for (const SegmentPipeline& existing : SegmentPipelines)
		{
			if (existing.Format == colorFormat)
			{
				return existing.Pipeline;
			}
		}

		SegmentPipeline made;
		made.Format = colorFormat;

		// A window's render pass as ImGui's backend makes it for an OS window, and VulkanSwapchain
		// for the main window. Load and store operations and layouts don't count for
		// compatibility; the attachment's format and samples, the subpass and the dependency do.
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = colorFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		vk::SubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.srcAccessMask = {};
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;
		made.RenderPass = vk::raii::RenderPass(Context->GetDevice(), renderPassInfo);
		made.Pipeline = CreatePipeline(*made.RenderPass);

		SegmentPipelines.push_back(std::move(made));
		return SegmentPipelines.back().Pipeline;
	}

	vk::raii::Pipeline VulkanPainter::CreatePipeline(vk::RenderPass renderPass)
	{
		const std::vector<uint32_t>& vertexCode = Programs->GetSpirv("Painter.vert");
		const std::vector<uint32_t>& fragmentCode = Programs->GetSpirv("Painter.frag");

		vk::ShaderModuleCreateInfo vertexModuleInfo{};
		vertexModuleInfo.codeSize = vertexCode.size() * sizeof(uint32_t);
		vertexModuleInfo.pCode = vertexCode.data();
		const vk::raii::ShaderModule vertexModule(Context->GetDevice(), vertexModuleInfo);

		vk::ShaderModuleCreateInfo fragmentModuleInfo{};
		fragmentModuleInfo.codeSize = fragmentCode.size() * sizeof(uint32_t);
		fragmentModuleInfo.pCode = fragmentCode.data();
		const vk::raii::ShaderModule fragmentModule(Context->GetDevice(), fragmentModuleInfo);

		std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
		stages[0].stage = vk::ShaderStageFlagBits::eVertex;
		stages[0].module = *vertexModule;
		stages[0].pName = "main";
		stages[1].stage = vk::ShaderStageFlagBits::eFragment;
		stages[1].module = *fragmentModule;
		stages[1].pName = "main";

		// No vertex buffers: Painter.vert makes its quad from the vertex index.
		vk::PipelineVertexInputStateCreateInfo vertexInput{};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// The shader blends all of a pixel's commands itself and writes premultiplied colour once;
		// only that result is blended over what the target holds.
		vk::PipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = vk::BlendFactor::eOne;
		blend.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blend.colorBlendOp = vk::BlendOp::eAdd;
		blend.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blend.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blend.alphaBlendOp = vk::BlendOp::eAdd;
		blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &blend;

		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *PipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		return vk::raii::Pipeline(Context->GetDevice(), Context->GetPipelineCache(), pipelineInfo);
	}

	void VulkanPainter::PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent)
	{
		if (*target.Image && target.Extent == extent)
		{
			return;
		}

		// The renderer waits for the previous frame's GPU work before recording, so the old image
		// is no longer in use.
		target.Framebuffer = nullptr;
		target.ImageView = nullptr;
		target.Image = nullptr;
		target.ImageMemory = nullptr;
		target.Extent = extent;

		vk::raii::Device& device = Context->GetDevice();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = TargetFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		// Painted, then shown (sampled) or copied out (the GPU tests)
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		target.Image = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = target.Image.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		target.ImageMemory = vk::raii::DeviceMemory(device, allocInfo);
		target.Image.bindMemory(*target.ImageMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *target.Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = TargetFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		target.ImageView = vk::raii::ImageView(device, viewInfo);

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = *RenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &*target.ImageView;
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		target.Framebuffer = vk::raii::Framebuffer(device, framebufferInfo);
		++target.Generation;
	}

	bool VulkanPainter::PrepareBuffer(VulkanPainterBuffers::GpuBuffer& buffer, vk::DeviceSize size, vk::BufferUsageFlags usage, bool bHostVisible)
	{
		if (*buffer.Buffer && buffer.Size >= size)
		{
			return false;
		}

		// Grow by doubling, so a frame that grows a little each time doesn't reallocate every
		// frame. The renderer waits for the frame before, so the old buffer is no longer in use.
		vk::DeviceSize newSize = std::max(buffer.Size, MinBufferSize);
		while (newSize < size)
		{
			newSize *= 2;
		}
		buffer.Mapped = nullptr;
		buffer.Buffer = nullptr;
		buffer.Memory = nullptr;

		// Host-visible and coherent for what the CPU writes or reads back, so it needs no flushing;
		// the GPU's own memory for the rest.
		CreateBuffer(*Context, newSize, usage,
			bHostVisible ? vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent : vk::MemoryPropertyFlagBits::eDeviceLocal,
			buffer.Buffer, buffer.Memory);
		buffer.Mapped = bHostVisible ? buffer.Memory.mapMemory(0, newSize) : nullptr;
		buffer.Size = newSize;
		return true;
	}

	void VulkanPainter::PrepareBuffers(VulkanPainterSegments& segments)
	{
		vk::raii::Device& device = Context->GetDevice();
		VulkanPainterBuffers& buffers = segments.Buffers;

		// The entries: as many as the tiles can need before the reduction, within the budget
		segments.EntryCapacity = static_cast<uint32_t>(std::clamp<uint64_t>(segments.MaxEntries, 1, std::max(segments.Budget.MaxTileEntries, 1u)));
		const vk::DeviceSize commandsSize = segments.Commands.size() * sizeof(PainterCommand);
		const vk::DeviceSize groupsSize = segments.Groups.size() * sizeof(PainterGroup);
		const vk::DeviceSize binCommandsSize = segments.BinCommands.size() * sizeof(PainterBinCommand);
		const vk::DeviceSize binSegmentsSize = segments.BinSegments.size() * sizeof(PainterBinSegment);
		const vk::DeviceSize tilesSize = std::max<vk::DeviceSize>(segments.TileCount, 1) * sizeof(PainterTile);
		const vk::DeviceSize entriesSize = static_cast<vk::DeviceSize>(segments.EntryCapacity) * sizeof(uint32_t);
		const vk::DeviceSize firstKeptSize = std::max<vk::DeviceSize>(segments.TileCount, 1) * sizeof(uint32_t);
		const vk::DeviceSize sumsSize = SumsHeaderSize + segments.Segments.size() * sizeof(PainterBinSums);

		using Usage = vk::BufferUsageFlagBits;
		PrepareBuffer(buffers.CommandsUpload, commandsSize, Usage::eTransferSrc, true);
		PrepareBuffer(buffers.GroupsUpload, groupsSize, Usage::eTransferSrc, true);
		PrepareBuffer(buffers.BinCommandsUpload, binCommandsSize, Usage::eTransferSrc, true);
		PrepareBuffer(buffers.BinSegmentsUpload, binSegmentsSize, Usage::eTransferSrc, true);
		PrepareBuffer(buffers.SumsReadBack, sumsSize, Usage::eTransferDst, true);
		if (segments.bReadBackTiles)
		{
			PrepareBuffer(buffers.TilesReadBack, tilesSize, Usage::eTransferDst, true);
			PrepareBuffer(buffers.EntriesReadBack, entriesSize, Usage::eTransferDst, true);
		}

		// The descriptor sets point at the GPU's own buffers, so they follow new ones.
		bool bNewBuffers = false;
		bNewBuffers |= PrepareBuffer(buffers.Commands, commandsSize, Usage::eStorageBuffer | Usage::eTransferDst, false);
		bNewBuffers |= PrepareBuffer(buffers.Groups, groupsSize, Usage::eStorageBuffer | Usage::eTransferDst, false);
		bNewBuffers |= PrepareBuffer(buffers.BinCommands, binCommandsSize, Usage::eStorageBuffer | Usage::eTransferDst, false);
		bNewBuffers |= PrepareBuffer(buffers.BinSegments, binSegmentsSize, Usage::eStorageBuffer | Usage::eTransferDst, false);
		bNewBuffers |= PrepareBuffer(buffers.Tiles, tilesSize, Usage::eStorageBuffer | Usage::eTransferSrc, false);
		bNewBuffers |= PrepareBuffer(buffers.Entries, entriesSize, Usage::eStorageBuffer | Usage::eTransferSrc, false);
		bNewBuffers |= PrepareBuffer(buffers.FirstKept, firstKeptSize, Usage::eStorageBuffer, false);
		bNewBuffers |= PrepareBuffer(buffers.Sums, sumsSize, Usage::eStorageBuffer | Usage::eTransferDst | Usage::eTransferSrc, false);

		const auto allocate = [&](vk::raii::DescriptorSet& set, const vk::raii::DescriptorSetLayout& layout)
		{
			if (*set)
			{
				return false;
			}
			vk::DescriptorSetAllocateInfo allocInfo{};
			allocInfo.descriptorPool = *DescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &*layout;
			set = std::move(vk::raii::DescriptorSets(device, allocInfo).front());
			return true;
		};
		bNewBuffers |= allocate(buffers.DrawSet, DescriptorSetLayout);
		bNewBuffers |= allocate(buffers.BinSet, BinSetLayout);
		if (bNewBuffers)
		{
			struct Binding
			{
				vk::DescriptorSet Set;
				uint32_t Index;
				const VulkanPainterBuffers::GpuBuffer& Buffer;
			};
			const Binding bindings[] = {
				{ *buffers.DrawSet, CommandsBinding, buffers.Commands },
				{ *buffers.DrawSet, GroupsBinding, buffers.Groups },
				{ *buffers.DrawSet, TilesBinding, buffers.Tiles },
				{ *buffers.DrawSet, EntriesBinding, buffers.Entries },
				{ *buffers.BinSet, BinSegmentsBinding, buffers.BinSegments },
				{ *buffers.BinSet, BinCommandsBinding, buffers.BinCommands },
				{ *buffers.BinSet, BinTilesBinding, buffers.Tiles },
				{ *buffers.BinSet, BinEntriesBinding, buffers.Entries },
				{ *buffers.BinSet, BinFirstKeptBinding, buffers.FirstKept },
				{ *buffers.BinSet, BinSumsBinding, buffers.Sums },
			};
			std::vector<vk::DescriptorBufferInfo> infos;
			infos.reserve(std::size(bindings));
			std::vector<vk::WriteDescriptorSet> writes;
			for (const Binding& binding : bindings)
			{
				infos.emplace_back(*binding.Buffer.Buffer, 0, VK_WHOLE_SIZE);
				vk::WriteDescriptorSet write{};
				write.dstSet = binding.Set;
				write.dstBinding = binding.Index;
				write.descriptorCount = 1;
				write.descriptorType = vk::DescriptorType::eStorageBuffer;
				write.pBufferInfo = &infos.back();
				writes.push_back(write);
			}
			device.updateDescriptorSets(writes, {});
		}

		// What the CPU wrote this frame, copied to the GPU's own buffers by Bin()
		const auto write = [](VulkanPainterBuffers::GpuBuffer& buffer, const void* data, vk::DeviceSize size)
		{
			if (size > 0)
			{
				std::memcpy(buffer.Mapped, data, static_cast<size_t>(size));
			}
		};
		write(buffers.CommandsUpload, segments.Commands.data(), commandsSize);
		write(buffers.GroupsUpload, segments.Groups.data(), groupsSize);
		write(buffers.BinCommandsUpload, segments.BinCommands.data(), binCommandsSize);
		write(buffers.BinSegmentsUpload, segments.BinSegments.data(), binSegmentsSize);
	}
}

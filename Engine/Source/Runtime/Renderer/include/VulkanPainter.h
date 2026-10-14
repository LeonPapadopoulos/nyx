#pragma once

#include "Painter.h"
#include "PainterDrawOptions.h"
#include "PainterTiles.h"
#include "ShaderPrograms.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	class VulkanPainter;

	// The buffers one set of segments (or a canvas) needs on the GPU, and the descriptor sets that
	// point at them. They only ever grow.
	//  - Written by the CPU each frame (host-visible), then copied into the GPU's own: the
	//    commands and groups the painter shader reads, and the measured commands and segments the
	//    binner reads.
	//  - The GPU's own (device-local): those copies, and what the binner writes, the tiles and
	//    their entries, each tile's first kept command between its passes, and its sums.
	//  - Read back (host-visible): the sums each frame, the tiles and entries while asked for.
	struct VulkanPainterBuffers
	{
		struct GpuBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			// Host-visible buffers stay mapped
			void* Mapped = nullptr;
			vk::DeviceSize Size = 0;
		};

		GpuBuffer CommandsUpload;
		GpuBuffer GroupsUpload;
		GpuBuffer BinCommandsUpload;
		GpuBuffer BinSegmentsUpload;

		GpuBuffer Commands;
		GpuBuffer Groups;
		GpuBuffer BinCommands;
		GpuBuffer BinSegments;
		GpuBuffer Tiles;
		GpuBuffer Entries;
		GpuBuffer FirstKept;
		GpuBuffer Sums;

		GpuBuffer SumsReadBack;
		GpuBuffer TilesReadBack;
		GpuBuffer EntriesReadBack;

		// Set 0 of the painter shader (commands, groups, tiles, entries), and of the binner
		vk::raii::DescriptorSet DrawSet{ nullptr };
		vk::raii::DescriptorSet BinSet{ nullptr };

		void Release();
	};

	// The segments of one frame (PainterSegment), from any number of painters, binned on the GPU
	// and drawn into render passes that already hold other drawing, e.g. ImGui's. Everything they
	// need on the GPU goes into one set of buffers: each painter's commands and groups, and each
	// segment's measured commands and tiles, one after the other. Drawing a segment only tells the
	// shader where its part starts.
	//
	//     segments.Clear(budget);                                          // each frame
	//     const uint32_t index = segments.Add(painter, segment);           // for each segment
	//     vulkanPainter.Bin(commandBuffer, segments);                      // once, outside a render pass
	//     vulkanPainter.DrawSegment(commandBuffer, segments, index, format, extent); // in a render pass
	//     ...
	//     segments.ReadBack();                                             // once the GPU is done
	//
	// Add() measures the segment's commands on the CPU (MeasurePainterSegment()) and copies what it
	// needs, so the painters may change or go afterwards. The textures are kept as ImGui texture
	// references and resolved when the segment is drawn. Release it (or let it go) before the
	// Vulkan device is destroyed.
	class VulkanPainterSegments
	{
	public:
		// Starts a new frame, with this frame's budget: drops all painters and segments, keeps the
		// buffers. What the last binning read back stays until the next ReadBack().
		void Clear(const PainterBudget& budget = PainterBudget{});

		// Packed colours (PackPremultipliedColor()) whose solid rects the statistics count as if
		// opaque (PainterBinningStats::EntriesIfAlsoOpaque), e.g. the docked cards'. They stay.
		void SetAlsoOpaqueColors(std::span<const uint32_t> colors);

		// Measures the segment and adds it, with its painter's commands, groups and textures if
		// this frame doesn't have them yet. Its grid covers the whole target with bWholeTarget (a
		// canvas), else only the tiles its commands touch. Returns the segment's index, for
		// DrawSegment().
		uint32_t Add(const Painter& painter, const PainterSegment& segment, bool bWholeTarget = false);

		uint32_t GetSegmentCount() const { return static_cast<uint32_t>(Segments.size()); }

		// One segment's binning statistics: the measuring's this frame, the GPU's since ReadBack()
		const PainterBinningStats& GetStats(uint32_t segment) const { return Segments[segment].Stats; }

		// What went wrong while measuring this frame's segments, and what the GPU found while
		// binning them (since ReadBack()), one line per problem
		const std::vector<std::string>& GetProblems() const { return Problems; }
		const std::vector<std::string>& GetReadBackProblems() const { return ReadBackProblems; }

		// Whether Bin() also reads back the tiles and entries, for GetBinned() and AddHeat(), e.g.
		// for the heatmap and the tests. Off by default: it copies all of them each frame.
		void SetReadBackTiles(bool bReadBack) { bReadBackTiles = bReadBack; }

		// Once the GPU has finished the frame Bin() was recorded in: takes what it read back into
		// the segments' statistics and problems, and with SetReadBackTiles(true) the tiles and
		// entries. Nothing if nothing was binned since the last call.
		void ReadBack();

		// A segment's tiles and entries as the GPU binned them, read back; empty without
		// SetReadBackTiles(true)
		PainterTiles GetBinned(uint32_t segment) const;

		// Adds the segment's tiles, each with its commands, to `grid`, a heat grid over the whole
		// target the segment is drawn into; from the tiles read back
		void AddHeat(uint32_t segment, PainterHeatGrid& grid) const;

		// The per-tile reduction over all segments, read back
		PainterReduction GetReduction() const;

		// What Bin() copies to the GPU this frame: commands, groups, measured commands and segments
		size_t GetUploadBytes() const;

		void Release();

	private:
		friend class VulkanPainter;

		// One painter's part of the buffers
		struct PainterPart
		{
			const Painter* Source = nullptr;
			uint32_t CommandBase = 0;
			uint32_t GroupBase = 0;
			std::vector<ImTextureRef> Textures;
		};

		// One segment's part of the buffers, and its tile grid on the target
		struct SegmentPart
		{
			uint32_t Painter = 0;
			uint32_t CommandBase = 0;
			uint32_t FirstTile = 0;
			uint32_t OriginX = 0;
			uint32_t OriginY = 0;
			uint32_t TilesX = 0;
			uint32_t TilesY = 0;
			PainterBinningStats Stats;
		};

		// The painter's part, added if this frame doesn't have it yet
		uint32_t FindOrAddPainter(const Painter& painter);

		PainterBudget Budget;
		std::vector<uint32_t> AlsoOpaque;

		std::vector<PainterCommand> Commands;
		std::vector<PainterGroup> Groups;
		std::vector<PainterBinCommand> BinCommands;
		std::vector<PainterBinSegment> BinSegments;
		// All segments' tiles, with the padding that starts each at a whole workgroup, and the
		// most entries they can need (within the budget)
		uint32_t TileCount = 0;
		uint64_t MaxEntries = 0;

		std::vector<PainterPart> Painters;
		std::vector<SegmentPart> Segments;
		std::vector<std::string> Problems;
		std::vector<std::string> ReadBackProblems;

		// Bin() recorded copies to read back; the tiles and entries too
		bool bReadBackTiles = false;
		bool bReadBackPending = false;
		bool bTilesReadBackPending = false;
		uint32_t EntryCapacity = 0;
		std::vector<PainterTile> ReadTiles;
		std::vector<uint32_t> ReadEntries;

		// Reused for measuring each segment
		PainterMeasuredSegment Measured;

		VulkanPainterBuffers Buffers;
	};

	// An image the painter draws into (a canvas): one segment over the whole target, binned and
	// drawn each frame.
	//
	// Empty until VulkanPainter::Draw() first draws into it, which also gives it the painter's
	// size. Release it (or let it go) before the Vulkan device is destroyed.
	class VulkanPainterTarget
	{
	public:
		// The painted image, in ShaderReadOnlyOptimal layout once drawn; null before the first
		// draw. It changes when the size changes, so read it after each draw.
		vk::Image GetImage() const { return *Image; }
		vk::ImageView GetImageView() const { return *ImageView; }
		vk::Extent2D GetExtent() const { return Extent; }

		// Changes each time the image is made anew. Tell a new image by this, not by its view:
		// Vulkan may give the new view the handle of the old one, destroyed just before.
		uint64_t GetGeneration() const { return Generation; }

		// Whether drawing also reads back the tiles and entries, for GetTiles()
		void SetReadBackTiles(bool bReadBack) { Segments.SetReadBackTiles(bReadBack); }

		// Once the GPU has finished the last draw: takes what it read back (VulkanPainterSegments::ReadBack())
		void ReadBack() { Segments.ReadBack(); }

		// The last drawn frame's grid and statistics, and with SetReadBackTiles(true) its tiles and
		// entries, as read back, e.g. for the Painter Test window
		PainterTiles GetTiles() const { return Segments.GetSegmentCount() > 0 ? Segments.GetBinned(0) : PainterTiles{}; }

		void Release();

	private:
		friend class VulkanPainter;

		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory ImageMemory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		// Counts the images made, across Release() (GetGeneration())
		uint64_t Generation = 0;

		VulkanPainterSegments Segments;
	};

	// Draws painted frames with the painter shader (Painter.vert and Painter.frag), after binning
	// them into tiles on the GPU (PainterBin.comp).
	//
	//     VulkanPainter vulkanPainter;
	//     vulkanPainter.Initialize(context, programs);
	//     ...
	//     vulkanPainter.Draw(commandBuffer, painter, target, options);
	//
	// It draws either a whole frame into an image of its own (Draw(), a canvas), or segments
	// into someone else's render pass (Bin() and DrawSegment(), e.g. ImGui's).
	//
	// The renderer owns one. It waits for the previous frame's GPU work before recording the next,
	// so every target and segment set has one set of buffers, rewritten each frame. A target is
	// drawn at most once per frame.
	class VulkanPainter
	{
	public:
		// Creates the canvases' render pass, the pipelines and the descriptor pool, and prepares
		// the empty texture (one short submission). The shaders, Painter.vert, Painter.frag and
		// PainterBin.comp, come from `programs`, which must outlive the painter.
		void Initialize(Nyx::VulkanContext& context, ShaderPrograms& programs);

		// Makes every pipeline again from the programs' current code, after they changed. The GPU
		// must be done with the old ones.
		void RebuildPipelines();
		void Shutdown();

		bool IsInitialized() const { return Context != nullptr; }

		// Records binning and painting `painter`'s frame into `target`, which takes the painter's
		// target size. Outside a render pass; the target's image ends in ShaderReadOnlyOptimal layout.
		void Draw(vk::raii::CommandBuffer& commandBuffer, const Painter& painter, VulkanPainterTarget& target, const PainterDrawOptions& options);

		// Records copying the frame's segments to the GPU and binning them, and reading back what
		// is read back. Once per frame, after the last Add() and before the first DrawSegment(),
		// outside a render pass. Drawing in later command buffers on the same queue finds them
		// binned.
		void Bin(vk::CommandBuffer commandBuffer, VulkanPainterSegments& segments);

		// Records drawing one segment, blended over what the render pass holds, inside a render
		// pass that is going on. It must be made like an ImGui window's: one colour attachment of
		// `colorFormat`, one sample, no depth, one subpass, and the one subpass dependency ImGui's
		// Vulkan backend and VulkanSwapchain give theirs (see GetSegmentPipeline()).
		// `framebufferExtent` is the render pass' size in pixels, which the segment's painter
		// recorded in. The first segment for a new colour format makes a pipeline for it.
		void DrawSegment(vk::CommandBuffer commandBuffer, const VulkanPainterSegments& segments, uint32_t segment, vk::Format colorFormat, vk::Extent2D framebufferExtent);

		// The descriptor set layout of one texture: a combined image sampler at binding 0, for the
		// fragment shader. It is defined exactly like ImGui's (imgui_impl_vulkan.cpp), so that the
		// sets ImGui makes for its textures bind as the painter's textures.
		const vk::raii::DescriptorSetLayout& GetTextureSetLayout() const { return TextureSetLayout; }

	private:
		// The segments' pipeline for window render passes of one colour format
		struct SegmentPipeline
		{
			vk::Format Format = vk::Format::eUndefined;
			// Only made to create the pipeline with; compatible with the render passes it runs in
			vk::raii::RenderPass RenderPass{ nullptr };
			vk::raii::Pipeline Pipeline{ nullptr };
		};

		void CheckDeviceLimits();
		void CreateRenderPass();
		void CreateDescriptorSetLayouts();
		void CreateDescriptorPool();
		void CreatePipelineLayout();
		void CreateBinPipeline();
		void CreateEmptyTexture();

		// The painter's pipeline, for render passes compatible with `renderPass`
		vk::raii::Pipeline CreatePipeline(vk::RenderPass renderPass);

		// The segments' pipeline for window render passes of this colour format, made on first
		// use. A pipeline runs in any render pass compatible with the one it was made with, and
		// Vulkan counts the subpass dependencies in that, not only the formats. ImGui's OS windows
		// and the main window (VulkanSwapchain) have the same single dependency, so one pipeline
		// per format serves them all.
		const vk::raii::Pipeline& GetSegmentPipeline(vk::Format colorFormat);

		// Binds the textures to sets 1 to PainterMaxTextures; unused slots, and textures ImGui
		// hasn't uploaded yet, get the empty texture.
		void BindTextures(vk::CommandBuffer commandBuffer, const std::vector<ImTextureRef>& textures);

		// Gives the target an image and framebuffer of this size, if it has none of that size yet
		void PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent);

		// Gives the segments' buffers room for this frame, and their descriptor sets
		void PrepareBuffers(VulkanPainterSegments& segments);

		// Gives `buffer` at least `size` bytes, growing by doubling; true if it was made anew
		bool PrepareBuffer(VulkanPainterBuffers::GpuBuffer& buffer, vk::DeviceSize size, vk::BufferUsageFlags usage, bool bHostVisible);

		Nyx::VulkanContext* Context = nullptr;
		ShaderPrograms* Programs = nullptr;

		// The canvases' render pass: clears the image, and leaves it ready to be shown
		vk::raii::RenderPass RenderPass{ nullptr };
		// Set 0: the frame's commands, groups, tiles and entries
		vk::raii::DescriptorSetLayout DescriptorSetLayout{ nullptr };
		// Sets 1 to PainterMaxTextures: one texture each
		vk::raii::DescriptorSetLayout TextureSetLayout{ nullptr };
		vk::raii::DescriptorPool DescriptorPool{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline CanvasPipeline{ nullptr };
		std::vector<SegmentPipeline> SegmentPipelines;

		// The binner (PainterBin.comp): its set (segments, measured commands, tiles, entries,
		// first kept commands, sums), layout and pipeline
		vk::raii::DescriptorSetLayout BinSetLayout{ nullptr };
		vk::raii::PipelineLayout BinPipelineLayout{ nullptr };
		vk::raii::Pipeline BinPipeline{ nullptr };

		// A transparent 1x1 texture for the texture slots a frame doesn't use: the shader reads
		// every slot, so every slot needs a texture.
		vk::raii::Image EmptyImage{ nullptr };
		vk::raii::DeviceMemory EmptyImageMemory{ nullptr };
		vk::raii::ImageView EmptyImageView{ nullptr };
		vk::raii::Sampler EmptySampler{ nullptr };
		vk::raii::DescriptorSet EmptyTextureSet{ nullptr };
	};
}

#include "NyxPCH.h"
#include "VulkanGrass.h"

#include "GrassFieldComponent.h"
#include "Log.h"
#include "Mesh.h"
#include "MeshData.h"
#include "ShaderPrograms.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Nyx
{
	namespace
	{
		constexpr const char* PointsProgram = "GrassPoints.slang";
		constexpr const char* BladesProgram = "Grass.slang";
		// GrassPoints.slang's and Grass.slang's numthreads
		constexpr uint32_t TileGroupSize = 32;

		// nyx.grass's GrassPoint and GrassBlade
		constexpr uint64_t PointBytes = 20;
		constexpr uint64_t BladeBytes = 36;

		// The most kinds of grass and tiles at once, and the tiles made in a frame, nearest first
		constexpr uint32_t MaxKinds = 32;
		constexpr uint32_t MaxTiles = 1024;
		constexpr uint32_t MaxTilesMadeAFrame = 16;
		// A tile's room over what its grid's cells need, for ground in layers
		constexpr float LayerRoom = 1.25f;
		// Where a tile's level is chosen from: this much nearer than its nearest camera, so it has the
		// blades before they are drawn; and, to give some back, this much nearer again
		constexpr float LevelMargin = 8.0f;
		constexpr float LevelKeepMargin = 16.0f;
		constexpr vk::DeviceSize FirstPointBytes = 64ull * 1024 * 1024;
		constexpr vk::DeviceSize MaxPointBytes = 1024ull * 1024 * 1024;
		// How many blades a view's buffer holds at first
		constexpr uint32_t FirstNearCapacity = 64 * 1024;
		constexpr uint32_t FirstFarCapacity = 256 * 1024;
		constexpr uint32_t MaxBlades = 8 * 1024 * 1024;

		// The blades' shapes: a strip of two vertices a ring and one at the tip
		constexpr uint32_t NearSegments = 7;
		constexpr uint32_t FarSegments = 3;

		// GrassPoints.slang's Tile
		struct TileConstants
		{
			glm::mat4 Model{ 1.0f };
			uint32_t Triangles = 0;
			uint32_t FirstPoint = 0;
			uint32_t Capacity = 0;
			uint32_t Slot = 0;
			float CellSize = 0.0f;
			uint32_t Level = 0;
			uint32_t Seed = 0;
			uint32_t MaterialMask = 0;
			glm::vec2 NormalYRange{ 0.0f };
			glm::vec2 HeightRange{ 0.0f };
			float ClumpSize = 0.0f;
			glm::vec2 Corner{ 0.0f };
			float Size = 0.0f;
		};
		static_assert(sizeof(TileConstants) == 128, "The most every device takes as push constants");

		// Grass.slang's Constants
		struct BladeConstants
		{
			glm::vec4 Planes[4]{};
			glm::vec2 Corner{ 0.0f };
			float PlaceUnit = 0.0f;
			uint32_t FirstPoint = 0;
			uint32_t Slot = 0;
			uint32_t Kind = 0;
			glm::vec2 Morph{ 0.0f };
			uint32_t NearCapacity = 0;
			uint32_t FarCapacity = 0;
			uint32_t FirstBlade = 0;
			uint32_t Segments = 0;
		};

		// nyx.grass's GrassKind
		struct GrassKindValues
		{
			float Height, HeightVariance, Width, WidthVariance;
			float Lean, LeanVariance, Bend, ClumpSize;
			float ClumpHeight, ClumpFacing, ClumpPull, ClumpColor;
			float PatchSize, PatchHeight, PatchDryness, ColorVariance;
			glm::vec3 RootColor;
			float WindResponse;
			glm::vec3 TipColor;
			float Stiffness;
			glm::vec3 DryColor;
			float Flutter;
			float NormalRounding, SelfShadow, Translucency, Roughness;
			float FullDensityDistance, Range;
			uint32_t Seed;
			uint32_t Padding;
		};
		static_assert(sizeof(GrassKindValues) == 144, "nyx.grass's GrassKind is laid out alike");

		float Radians(float degrees)
		{
			return degrees * std::numbers::pi_v<float> / 180.0f;
		}

		// The largest a blade of these values can be, to cull tiles by
		float GetMaxHeight(const GrassFieldComponent& component)
		{
			return std::max(component.Height, 0.01f) * (1.0f + std::clamp(component.HeightVariance, 0.0f, 1.0f)) * (1.0f + std::clamp(component.PatchHeight, 0.0f, 1.0f));
		}

		float GetClumpSize(const GrassFieldComponent& component)
		{
			return std::max(component.ClumpSize, 0.05f);
		}

		GrassKindValues MakeKindValues(const GrassFieldComponent& component, const GrassSettings& settings)
		{
			GrassKindValues values{};
			values.Height = std::max(component.Height, 0.01f);
			values.HeightVariance = std::clamp(component.HeightVariance, 0.0f, 1.0f);
			values.Width = std::max(component.Width, 0.001f);
			values.WidthVariance = std::clamp(component.WidthVariance, 0.0f, 1.0f);
			values.Lean = Radians(std::clamp(component.Lean, 0.0f, 85.0f));
			values.LeanVariance = Radians(std::clamp(component.LeanVariance, 0.0f, 85.0f));
			values.Bend = std::clamp(component.Bend, 0.0f, 1.0f);
			values.ClumpSize = GetClumpSize(component);
			values.ClumpHeight = std::clamp(component.ClumpHeight, 0.0f, 1.0f);
			values.ClumpFacing = std::clamp(component.ClumpFacing, 0.0f, 1.0f);
			values.ClumpPull = std::clamp(component.ClumpPull, -1.0f, 1.0f);
			values.ClumpColor = std::clamp(component.ClumpColor, 0.0f, 1.0f);
			values.PatchSize = std::max(component.PatchSize, 0.5f);
			values.PatchHeight = std::clamp(component.PatchHeight, 0.0f, 1.0f);
			values.PatchDryness = std::clamp(component.PatchDryness, 0.0f, 1.0f);
			values.ColorVariance = std::clamp(component.ColorVariance, 0.0f, 1.0f);
			values.RootColor = component.RootColor;
			values.WindResponse = std::clamp(component.WindResponse, 0.0f, 2.0f);
			values.TipColor = component.TipColor;
			values.Stiffness = std::max(component.Stiffness, 0.05f);
			values.DryColor = component.DryColor;
			values.Flutter = std::clamp(component.Flutter, 0.0f, 1.0f);
			values.NormalRounding = std::clamp(component.NormalRounding, 0.0f, 1.0f);
			values.SelfShadow = std::clamp(component.SelfShadow, 0.0f, 1.0f);
			values.Translucency = std::clamp(component.Translucency, 0.0f, 1.0f);
			values.Roughness = std::clamp(component.Roughness, 0.05f, 1.0f);
			values.FullDensityDistance = settings.FullDensityDistance;
			values.Range = settings.Range;
			values.Seed = settings.Seed;
			return values;
		}

		uint64_t Mix(uint64_t hash, const void* data, size_t bytes)
		{
			const auto* byte = static_cast<const uint8_t*>(data);
			for (size_t index = 0; index < bytes; ++index)
			{
				hash = (hash ^ byte[index]) * 1099511628211ull;
			}
			return hash;
		}

		template <typename T>
		uint64_t Mix(uint64_t hash, const T& value)
		{
			return Mix(hash, &value, sizeof(T));
		}

		struct DeviceBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
		};

		// A compute write, seen by later reads at these stages
		void WriteBarrier(vk::raii::CommandBuffer& cmd, vk::PipelineStageFlags destination, vk::AccessFlags access)
		{
			const vk::MemoryBarrier barrier(vk::AccessFlagBits::eShaderWrite, access);
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, destination, {}, barrier, {}, {});
		}
	}

	size_t VulkanGrass::TileKeyHash::operator()(const TileKey& key) const
	{
		uint64_t hash = 14695981039346656037ull;
		hash = Mix(hash, key.World);
		hash = Mix(hash, key.Entity);
		hash = Mix(hash, key.Data);
		return static_cast<size_t>(hash);
	}

	VulkanGrass::VulkanGrass(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanGrass::Initialize(vk::DescriptorSetLayout bindlessLayout, vk::DescriptorSetLayout viewLayout)
	{
		BindlessLayout = bindlessLayout;
		ViewLayout = viewLayout;
		vk::raii::Device& device = Context.GetDevice();

		// A tile's making: the mesh's vertices and indices, the points, the counts
		std::array<vk::DescriptorSetLayoutBinding, 4> tileBindings{};
		for (uint32_t index = 0; index < tileBindings.size(); ++index)
		{
			tileBindings[index] = vk::DescriptorSetLayoutBinding(index, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		}
		TileSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, tileBindings));
		const vk::DescriptorSetLayout tileLayout = *TileSetLayout;
		const vk::PushConstantRange tileRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(TileConstants));
		TilePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, tileLayout, tileRange));
		const vk::DescriptorPoolSize tilePoolSize(vk::DescriptorType::eStorageBuffer, MaxTilesMadeAFrame * 4);
		TileSets = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo({}, MaxTilesMadeAFrame, tilePoolSize));

		// A view's blades and their draws: the points, the kinds, the blades written, the arguments,
		// the blades read, the tiles' counts
		const vk::ShaderStageFlags compute = vk::ShaderStageFlagBits::eCompute;
		const std::array<vk::DescriptorSetLayoutBinding, 6> bladeBindings = {
			vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, compute),
			vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, compute | vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
			vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, compute),
			vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, compute),
			vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex),
			vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, compute),
		};
		BladeSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bladeBindings));
		const std::array<vk::DescriptorSetLayout, 3> bladeLayouts = { BindlessLayout, ViewLayout, *BladeSetLayout };
		const vk::PushConstantRange bladeRange(compute | vk::ShaderStageFlagBits::eVertex, 0, sizeof(BladeConstants));
		BladePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, bladeLayouts, bladeRange));

		FreeSlots.reserve(MaxTiles);
		for (uint32_t slot = MaxTiles; slot > 0; --slot)
		{
			FreeSlots.push_back(slot - 1);
		}
		CreateComputePipelines();
	}

	void VulkanGrass::Shutdown()
	{
		Tiles.clear();
		TilesToMake.clear();
		PointRanges.reset();
		Points = nullptr;
		PointsMemory = nullptr;
		TileCounts = nullptr;
		TileCountsMemory = nullptr;
		KindValues = nullptr;
		KindValuesMemory = nullptr;
		KindValuesMapped = nullptr;
		DrawPipeline = nullptr;
		BladesDonePipeline = nullptr;
		BladesPipeline = nullptr;
		BladePipelineLayout = nullptr;
		BladeSetLayout = nullptr;
		TileSets = nullptr;
		PointsDonePipeline = nullptr;
		PointsPipeline = nullptr;
		TilePipelineLayout = nullptr;
		TileSetLayout = nullptr;
	}

	void VulkanGrass::CreateComputePipelines()
	{
		auto make = [this](const char* program, const char* entryPoint, const vk::raii::PipelineLayout& layout)
		{
			const std::vector<uint32_t>& spirv = Programs.GetSpirv(program, entryPoint);
			const vk::raii::ShaderModule module = CreateShaderModule(Context, spirv);
			vk::ComputePipelineCreateInfo info{};
			info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint);
			info.layout = *layout;
			return vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), info);
		};
		PointsPipeline = make(PointsProgram, "grassPoints", TilePipelineLayout);
		PointsDonePipeline = make(PointsProgram, "grassPointsDone", TilePipelineLayout);
		BladesPipeline = make(BladesProgram, "grassBlades", BladePipelineLayout);
		BladesDonePipeline = make(BladesProgram, "grassBladesDone", BladePipelineLayout);
		for (auto& [key, tile] : Tiles)
		{
			tile.MadeFrom = 0;
		}
	}

	void VulkanGrass::CreateDrawPipeline(vk::RenderPass scenePass, vk::SampleCountFlagBits samples)
	{
		const vk::raii::ShaderModule vertex = CreateShaderModule(Context, Programs.GetSpirv(BladesProgram, "grassVertex"));
		const vk::raii::ShaderModule fragment = CreateShaderModule(Context, Programs.GetSpirv(BladesProgram, "grassFragment"));
		const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertex, "grassVertex"),
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragment, "grassFragment"),
		};
		const vk::PipelineVertexInputStateCreateInfo vertexInput{};
		const vk::PipelineInputAssemblyStateCreateInfo assembly({}, vk::PrimitiveTopology::eTriangleStrip);
		vk::PipelineViewportStateCreateInfo viewport{};
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = samples;
		vk::PipelineDepthStencilStateCreateInfo depth{};
		depth.depthTestEnable = VK_TRUE;
		depth.depthWriteEnable = VK_TRUE;
		depth.depthCompareOp = DepthTestNearer;
		vk::PipelineColorBlendAttachmentState attachment{};
		attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		const std::array<vk::PipelineColorBlendAttachmentState, 2> attachments = { attachment, attachment };
		vk::PipelineColorBlendStateCreateInfo blending{};
		blending.attachmentCount = static_cast<uint32_t>(attachments.size());
		blending.pAttachments = attachments.data();
		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamic{};
		dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamic.pDynamicStates = dynamicStates.data();

		vk::GraphicsPipelineCreateInfo info{};
		info.stageCount = static_cast<uint32_t>(stages.size());
		info.pStages = stages.data();
		info.pVertexInputState = &vertexInput;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &rasterizer;
		info.pMultisampleState = &multisampling;
		info.pDepthStencilState = &depth;
		info.pColorBlendState = &blending;
		info.pDynamicState = &dynamic;
		info.layout = *BladePipelineLayout;
		info.renderPass = scenePass;
		info.subpass = 0;
		DrawPipeline = vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), info);
	}

	void VulkanGrass::MakeBuffers()
	{
		if (*TileCounts != VK_NULL_HANDLE)
		{
			return;
		}
		CreateBuffer(Context, MaxTiles * sizeof(uint32_t) * 4, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal, TileCounts, TileCountsMemory);
		CreateBuffer(Context, MaxKinds * sizeof(GrassKindValues), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			KindValues, KindValuesMemory);
		KindValuesMapped = KindValuesMemory.mapMemory(0, VK_WHOLE_SIZE);
		CreateBuffer(Context, FirstPointBytes, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal, Points, PointsMemory);
		PointRanges = std::make_unique<RangeAllocator>(FirstPointBytes);
		++PointsGeneration;
	}

	void VulkanGrass::BeginFrame()
	{
		++Frame;
		KindCount = 0;
		TilesToMake.clear();
		CurrentStats = {};
		if (*TileSets != VK_NULL_HANDLE)
		{
			TileSets.reset();
		}
	}

	void VulkanGrass::AddKind(uint64_t worldRevision, uint64_t entity, const GrassFieldComponent& component, const std::vector<GrassSurfaceMesh>& meshes, const std::vector<glm::vec3>& cameras)
	{
		const GrassSettings settings = MakeGrassSettings(component);
		if (!settings.bGrass || cameras.empty() || KindCount >= MaxKinds)
		{
			return;
		}
		MakeBuffers();
		const uint32_t kind = KindCount++;
		const GrassKindValues values = MakeKindValues(component, settings);
		std::memcpy(static_cast<GrassKindValues*>(KindValuesMapped) + kind, &values, sizeof(values));

		const float clumpSize = GetClumpSize(component);
		const float maxHeight = GetMaxHeight(component);
		uint64_t pointsFrom = 14695981039346656037ull;
		pointsFrom = Mix(pointsFrom, settings.Seed);
		pointsFrom = Mix(pointsFrom, settings.CellSize);
		pointsFrom = Mix(pointsFrom, settings.NormalYRange);
		pointsFrom = Mix(pointsFrom, settings.HeightRange);
		pointsFrom = Mix(pointsFrom, settings.MaterialMask);
		pointsFrom = Mix(pointsFrom, clumpSize);

		for (const GrassSurfaceMesh& mesh : meshes)
		{
			float nearest = FLT_MAX;
			for (const glm::vec3& camera : cameras)
			{
				nearest = std::min(nearest, GetBoxDistance(mesh.WorldBounds, camera));
			}
			if (nearest > settings.Range + LevelKeepMargin)
			{
				continue;
			}
			Tile& tile = Tiles[TileKey{ worldRevision, entity, mesh.Data.get() }];
			tile.Data = mesh.Data;
			tile.Gpu = mesh.Gpu;
			tile.Kind = kind;
			tile.WorldRevision = worldRevision;
			tile.LastFrame = Frame;
			tile.Nearest = nearest;
			tile.Range = settings.Range;
			tile.Settings = settings;
			tile.ClumpSize = clumpSize;
			tile.MaxHeight = maxHeight;
			tile.Model = mesh.Model;
			tile.WorldBounds = mesh.WorldBounds;
			tile.Morph = mesh.Morph;
			if (!mesh.bHasMaterials)
			{
				tile.Settings.MaterialMask = ~0u;
			}
			uint64_t madeFrom = pointsFrom;
			madeFrom = Mix(madeFrom, mesh.Revision);
			madeFrom = Mix(madeFrom, mesh.Model);
			tile.WantedFrom = madeFrom;

			// A level it can make do with and one it should give back blades for
			const uint32_t needed = GetGrassGridLevelForShare(GetGrassDrawnShare(settings, std::max(nearest - LevelMargin, 0.0f)));
			const uint32_t coarser = GetGrassGridLevelForShare(GetGrassDrawnShare(settings, std::max(nearest - LevelKeepMargin, 0.0f)));
			tile.WantedLevel = !tile.bMade || needed < tile.Level ? needed : (coarser > tile.Level ? coarser : tile.Level);
		}
	}

	void VulkanGrass::FreeTileRange(Tile& tile)
	{
		if (tile.bAllocated)
		{
			PointRanges->Free(tile.FirstByte, tile.Bytes);
			FreeSlots.push_back(tile.Slot);
			tile.bAllocated = false;
		}
		tile.bMade = false;
	}

	void VulkanGrass::EndKinds()
	{
		// Tiles nothing needs now give their room back first
		for (auto it = Tiles.begin(); it != Tiles.end();)
		{
			if (it->second.LastFrame != Frame)
			{
				FreeTileRange(it->second);
				it = Tiles.erase(it);
			}
			else
			{
				++it;
			}
		}

		// The nearest ones due, so the ground under the cameras never waits
		std::vector<Tile*> due;
		for (auto& [key, tile] : Tiles)
		{
			if (!tile.bMade || tile.MadeFrom != tile.WantedFrom || tile.Level != tile.WantedLevel)
			{
				due.push_back(&tile);
			}
		}
		std::sort(due.begin(), due.end(), [](const Tile* a, const Tile* b) { return a->Nearest < b->Nearest; });

		for (Tile* tile : due)
		{
			if (TilesToMake.size() >= MaxTilesMadeAFrame)
			{
				++CurrentStats.TilesWaiting;
				continue;
			}
			const float size = std::max(tile->WorldBounds.Max.x - tile->WorldBounds.Min.x, tile->WorldBounds.Max.z - tile->WorldBounds.Min.z) + 2.0f * tile->Settings.CellSize;
			const float cells = size / tile->Settings.CellSize;
			const uint64_t levelCells = static_cast<uint64_t>(cells * cells * LayerRoom) >> (2 * tile->WantedLevel);
			const uint32_t capacity = static_cast<uint32_t>(std::clamp<uint64_t>(levelCells, 64, 64ull * 1024 * 1024));
			const uint64_t bytes = capacity * PointBytes;

			if (tile->bAllocated)
			{
				const bool bSameRoom = tile->Bytes == bytes;
				if (!bSameRoom)
				{
					FreeTileRange(*tile);
				}
			}
			if (!tile->bAllocated)
			{
				std::optional<uint64_t> first = FreeSlots.empty() ? std::nullopt : PointRanges->Allocate(bytes, PointBytes);
				if (!first && !FreeSlots.empty() && PointRanges->GetSize() * 2 <= MaxPointBytes)
				{
					// Twice the room: every tile is made again in it, nearest first
					const uint64_t larger = PointRanges->GetSize() * 2;
					LOG_INFO("The grass's ground points take {} MiB", larger / (1024 * 1024));
					for (auto& [key, other] : Tiles)
					{
						if (other.bAllocated)
						{
							FreeSlots.push_back(other.Slot);
							other.bAllocated = false;
						}
						other.bMade = false;
					}
					TilesToMake.clear();
					Points = nullptr;
					PointsMemory = nullptr;
					CreateBuffer(Context, larger, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal, Points, PointsMemory);
					PointRanges = std::make_unique<RangeAllocator>(larger);
					++PointsGeneration;
					first = PointRanges->Allocate(bytes, PointBytes);
				}
				if (!first)
				{
					if (!bLoggedFull)
					{
						LOG_WARNING("The grass has no room for all of its tiles: the farthest aren't drawn");
						bLoggedFull = true;
					}
					++CurrentStats.TilesWaiting;
					continue;
				}
				tile->FirstByte = *first;
				tile->Bytes = bytes;
				tile->Capacity = capacity;
				tile->Slot = FreeSlots.back();
				FreeSlots.pop_back();
				tile->bAllocated = true;
			}
			tile->MadeFrom = tile->WantedFrom;
			tile->Level = tile->WantedLevel;
			tile->Corner = glm::vec2(tile->WorldBounds.Min.x, tile->WorldBounds.Min.z) - glm::vec2(tile->Settings.CellSize);
			tile->PlaceSize = size;
			TilesToMake.push_back(tile);
		}

		CurrentStats.Tiles = static_cast<uint32_t>(Tiles.size());
		CurrentStats.TilesMade = static_cast<uint32_t>(TilesToMake.size());
		CurrentStats.PointBytes = PointRanges ? PointRanges->GetSize() : 0;
		CurrentStats.TileBytes = PointRanges ? PointRanges->GetSize() - PointRanges->GetFreeBytes() : 0;
	}

	void VulkanGrass::RecordTiles(vk::raii::CommandBuffer& cmd)
	{
		if (TilesToMake.empty())
		{
			return;
		}
		for (const Tile* tile : TilesToMake)
		{
			cmd.fillBuffer(*TileCounts, tile->Slot * sizeof(uint32_t) * 4, sizeof(uint32_t) * 4, 0);
		}
		const vk::MemoryBarrier cleared(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {}, cleared, {}, {});

		vk::raii::Device& device = Context.GetDevice();
		const vk::DescriptorSetLayout setLayout = *TileSetLayout;
		std::vector<vk::DescriptorSet> sets;
		sets.reserve(TilesToMake.size());
		auto constantsOf = [](const Tile& tile)
		{
			TileConstants constants;
			constants.Model = tile.Model;
			constants.Triangles = tile.Gpu->GetIndexCount() / 3;
			constants.FirstPoint = static_cast<uint32_t>(tile.FirstByte / PointBytes);
			constants.Capacity = tile.Capacity;
			constants.Slot = tile.Slot;
			constants.CellSize = tile.Settings.CellSize;
			constants.Level = tile.Level;
			constants.Seed = tile.Settings.Seed;
			constants.MaterialMask = tile.Settings.MaterialMask;
			constants.NormalYRange = tile.Settings.NormalYRange;
			constants.HeightRange = tile.Settings.HeightRange;
			constants.ClumpSize = tile.ClumpSize;
			constants.Corner = tile.Corner;
			constants.Size = tile.PlaceSize;
			return constants;
		};

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *PointsPipeline);
		for (const Tile* tile : TilesToMake)
		{
			const vk::DescriptorSet set = (*device).allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*TileSets, setLayout)).front();
			sets.push_back(set);
			const vk::DescriptorBufferInfo vertices(tile->Gpu->GetVertexBuffer(), 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo indices(tile->Gpu->GetIndexBuffer(), 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo points(*Points, 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo counts(*TileCounts, 0, VK_WHOLE_SIZE);
			const std::array<vk::WriteDescriptorSet, 4> writes = {
				vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &vertices),
				vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indices),
				vk::WriteDescriptorSet(set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &points),
				vk::WriteDescriptorSet(set, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &counts),
			};
			device.updateDescriptorSets(writes, {});
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *TilePipelineLayout, 0, set, {});
			const TileConstants constants = constantsOf(*tile);
			cmd.pushConstants<TileConstants>(*TilePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, constants);
			cmd.dispatch((constants.Triangles + TileGroupSize - 1) / TileGroupSize, 1, 1);
		}

		WriteBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *PointsDonePipeline);
		for (size_t index = 0; index < TilesToMake.size(); ++index)
		{
			const Tile& tile = *TilesToMake[index];
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *TilePipelineLayout, 0, sets[index], {});
			const TileConstants constants = constantsOf(tile);
			cmd.pushConstants<TileConstants>(*TilePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, constants);
			cmd.dispatch(1, 1, 1);
		}
		WriteBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eDrawIndirect, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);

		for (Tile* tile : TilesToMake)
		{
			tile->bMade = true;
		}
		TilesToMake.clear();
	}

	void VulkanGrass::MakeViewBuffers(SceneViewGrass& grass, uint32_t nearCapacity, uint32_t farCapacity)
	{
		vk::raii::Device& device = Context.GetDevice();
		if (*grass.Arguments == VK_NULL_HANDLE)
		{
			CreateBuffer(Context, sizeof(uint32_t) * 8, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eDeviceLocal, grass.Arguments, grass.ArgumentsMemory);
			CreateBuffer(Context, sizeof(uint32_t) * 8, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, grass.Readback, grass.ReadbackMemory);
			grass.ReadbackMapped = grass.ReadbackMemory.mapMemory(0, VK_WHOLE_SIZE);
			const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 6);
			grass.Pool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo({}, 1, poolSize));
			const vk::DescriptorSetLayout setLayout = *BladeSetLayout;
			grass.Set = (*device).allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*grass.Pool, setLayout)).front();
		}
		grass.Blades = nullptr;
		grass.BladesMemory = nullptr;
		grass.NearCapacity = nearCapacity;
		grass.FarCapacity = farCapacity;
		CreateBuffer(Context, (nearCapacity + uint64_t(farCapacity)) * BladeBytes, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal, grass.Blades, grass.BladesMemory);
		grass.PointsGeneration = 0;
	}

	void VulkanGrass::WriteViewSet(SceneViewGrass& grass)
	{
		const vk::DescriptorBufferInfo points(*Points, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo kinds(*KindValues, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo blades(*grass.Blades, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo arguments(*grass.Arguments, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo counts(*TileCounts, 0, VK_WHOLE_SIZE);
		const std::array<vk::WriteDescriptorSet, 6> writes = {
			vk::WriteDescriptorSet(grass.Set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &points),
			vk::WriteDescriptorSet(grass.Set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &kinds),
			vk::WriteDescriptorSet(grass.Set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &blades),
			vk::WriteDescriptorSet(grass.Set, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &arguments),
			vk::WriteDescriptorSet(grass.Set, 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &blades),
			vk::WriteDescriptorSet(grass.Set, 5, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &counts),
		};
		Context.GetDevice().updateDescriptorSets(writes, {});
		grass.PointsGeneration = PointsGeneration;
	}

	void VulkanGrass::RecordBlades(SceneViewGrass& grass, uint64_t worldRevision, const glm::vec3& camera, const Frustum& frustum, vk::DescriptorSet bindlessSet, vk::DescriptorSet viewSet, vk::raii::CommandBuffer& cmd)
	{
		grass.bDrawn = false;
		if (KindCount == 0)
		{
			return;
		}
		bool bAny = false;
		for (const auto& [key, tile] : Tiles)
		{
			bAny = bAny || (tile.WorldRevision == worldRevision && tile.bMade);
		}
		if (!bAny)
		{
			return;
		}
		if (*grass.Blades == VK_NULL_HANDLE)
		{
			MakeViewBuffers(grass, FirstNearCapacity, FirstFarCapacity);
		}
		else if (grass.bGrow)
		{
			MakeViewBuffers(grass, std::min(grass.NearCapacity * 2, MaxBlades), std::min(grass.FarCapacity * 2, MaxBlades));
			grass.bGrow = false;
		}
		if (grass.PointsGeneration != PointsGeneration)
		{
			WriteViewSet(grass);
		}

		// The draws start with no blades
		const std::array<uint32_t, 8> arguments = { NearSegments * 2 + 1, 0, 0, 0, FarSegments * 2 + 1, 0, 0, 0 };
		cmd.updateBuffer<uint32_t>(*grass.Arguments, 0, arguments);
		const vk::MemoryBarrier cleared(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {}, cleared, {}, {});

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *BladesPipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *BladePipelineLayout, 0, { bindlessSet, viewSet, grass.Set }, {});
		BladeConstants constants;
		for (size_t plane = 0; plane < 4; ++plane)
		{
			constants.Planes[plane] = frustum.Planes[plane];
		}
		constants.NearCapacity = grass.NearCapacity;
		constants.FarCapacity = grass.FarCapacity;
		for (const auto& [key, tile] : Tiles)
		{
			if (tile.WorldRevision != worldRevision || !tile.bMade)
			{
				continue;
			}
			const BoundingBox reach = ExpandBox(tile.WorldBounds, tile.MaxHeight);
			if (GetBoxDistance(reach, camera) > tile.Range || !IsBoxVisible(frustum, reach))
			{
				continue;
			}
			constants.Corner = tile.Corner;
			constants.PlaceUnit = tile.PlaceSize / 65535.0f;
			constants.FirstPoint = static_cast<uint32_t>(tile.FirstByte / PointBytes);
			constants.Slot = tile.Slot;
			constants.Kind = tile.Kind;
			constants.Morph = tile.Morph;
			cmd.pushConstants<BladeConstants>(*BladePipelineLayout, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex, 0, constants);
			cmd.dispatchIndirect(*TileCounts, tile.Slot * sizeof(uint32_t) * 4);
		}

		WriteBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *BladesDonePipeline);
		cmd.pushConstants<BladeConstants>(*BladePipelineLayout, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex, 0, constants);
		cmd.dispatch(1, 1, 1);
		WriteBarrier(cmd, vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eTransferRead);

		cmd.copyBuffer(*grass.Arguments, *grass.Readback, vk::BufferCopy(0, 0, sizeof(uint32_t) * 8));
		grass.bReadbackValid = true;
		grass.bDrawn = true;
	}

	void VulkanGrass::Draw(const SceneViewGrass& grass, vk::DescriptorSet bindlessSet, vk::DescriptorSet viewSet, vk::raii::CommandBuffer& cmd)
	{
		if (!grass.bDrawn)
		{
			return;
		}
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *DrawPipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *BladePipelineLayout, 0, { bindlessSet, viewSet, grass.Set }, {});
		BladeConstants constants;
		constants.FirstBlade = 0;
		constants.Segments = NearSegments;
		cmd.pushConstants<BladeConstants>(*BladePipelineLayout, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex, 0, constants);
		cmd.drawIndirect(*grass.Arguments, 0, 1, sizeof(uint32_t) * 4);
		constants.FirstBlade = grass.NearCapacity;
		constants.Segments = FarSegments;
		cmd.pushConstants<BladeConstants>(*BladePipelineLayout, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex, 0, constants);
		cmd.drawIndirect(*grass.Arguments, sizeof(uint32_t) * 4, 1, sizeof(uint32_t) * 4);
	}

	void VulkanGrass::ReadStats(SceneViewGrass& grass)
	{
		if (!grass.bReadbackValid)
		{
			grass.NearBlades = 0;
			grass.FarBlades = 0;
			return;
		}
		grass.bReadbackValid = false;
		const auto* arguments = static_cast<const uint32_t*>(grass.ReadbackMapped);
		grass.NearBlades = arguments[1];
		grass.FarBlades = arguments[5];
		// Full: some didn't fit, so there is more room next frame
		grass.bGrow = (grass.NearBlades >= grass.NearCapacity && grass.NearCapacity < MaxBlades) || (grass.FarBlades >= grass.FarCapacity && grass.FarCapacity < MaxBlades);
	}

	void VulkanGrass::Release(SceneViewGrass& grass)
	{
		grass = SceneViewGrass{};
	}
}

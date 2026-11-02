#pragma once

#include "Grass.h"
#include "JobSystem.h"
#include "MeshMemoryPool.h"
#include "ScatterInstances.h"
#include "SceneCulling.h"

#include <glm/glm.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class Mesh;
	class ShaderPrograms;
	class VulkanContext;
	struct GrassFieldComponent;
	struct MeshData;

	// A surface's mesh a world's grass grows on this frame (VulkanRenderer::PrepareGrass())
	struct GrassSurfaceMesh
	{
		// Kept alive by the tile made from it, so no other data is made at its address
		std::shared_ptr<const MeshData> Data;
		uint64_t Revision = 0;
		Mesh* Gpu = nullptr;
		glm::mat4 Model{ 1.0f };
		BoundingBox WorldBounds;
		// A terrain chunk's slide with the level of detail, in the world (TerrainChunk::Morph)
		glm::vec2 Morph{ 0.0f };
		// An SDF terrain's: its vertices have material numbers
		bool bHasMaterials = false;
		// Which of its triangles: all of them, or a part of a large one cut up (GetMeshParts())
		uint32_t Part = 0;
		vk::Buffer PartIndices;
		vk::DeviceSize PartFirstByte = 0;
		uint32_t Triangles = 0;
	};

	// A large mesh's triangles by squares of its X and Z, so each square is a tile: a mesh that isn't
	// made in chunks, such as a ground's. Cut on a worker when first asked for, and kept for its
	// revision.
	struct GrassMeshPart
	{
		BoundingBox LocalBounds;
		vk::DeviceSize FirstByte = 0;
		uint32_t Triangles = 0;
	};

	// How wide the squares are, as a terrain's finest chunks
	inline constexpr float GrassMeshPartSize = 32.0f;

	// A view's grass on the GPU: the blades it draws this frame and the arguments of the draws. Made
	// at the first frame it has grass, and larger when the blades don't fit.
	struct SceneViewGrass
	{
		vk::raii::Buffer Blades{ nullptr };
		vk::raii::DeviceMemory BladesMemory{ nullptr };
		vk::raii::Buffer Arguments{ nullptr };
		vk::raii::DeviceMemory ArgumentsMemory{ nullptr };
		// What the last frames counted, for the stats, read one frame late
		vk::raii::Buffer Readback{ nullptr };
		vk::raii::DeviceMemory ReadbackMemory{ nullptr };
		void* ReadbackMapped = nullptr;
		vk::raii::DescriptorPool Pool{ nullptr };
		vk::DescriptorSet Set;
		uint32_t NearCapacity = 0;
		uint32_t FarCapacity = 0;
		bool bDrawn = false;
		bool bReadbackValid = false;
		// The blades it drew the frame before, near and far, as the buffer held them
		uint32_t NearBlades = 0;
		uint32_t FarBlades = 0;
		// The buffer was full last frame
		bool bGrow = false;
		// The ground points' buffer the set was written for
		uint64_t PointsGeneration = 0;
	};

	// The grass on the GPU (GrassFieldComponent, Docs/Features.md): for each tile near a camera, one for
	// each mesh of its surface, the places a blade can grow are made once from the mesh's triangles,
	// in compute, and kept in one buffer, until the mesh, the values that decide them or how many of
	// them the tile needs changes. Each frame, for each view, the tiles in its frustum go through a
	// second compute pass that thins, culls and shapes their blades into a buffer of the view's, drawn
	// with two indirect draws: only the tiles cost the CPU anything, however many blades there are.
	class VulkanGrass
	{
	public:
		VulkanGrass(VulkanContext& context, ShaderPrograms& programs);

		// The set layouts of the bindless array and the views, which must outlive it
		void Initialize(vk::DescriptorSetLayout bindlessLayout, vk::DescriptorSetLayout viewLayout);
		void Shutdown();
		// When a compute shader changed: every tile is made again
		void CreateComputePipelines();
		// When the draw's shader or the scene pass changed
		void CreateDrawPipeline(vk::RenderPass scenePass, vk::SampleCountFlagBits samples);

		// Before a frame's kinds are added, once its last frame is done
		void BeginFrame();
		// A kind of grass this frame, with the cameras of the views that show it, and the tiles it needs
		void AddKind(uint64_t worldRevision, uint64_t entity, const GrassFieldComponent& component, const std::vector<GrassSurfaceMesh>& meshes, const std::vector<glm::vec3>& cameras);
		// The scatter blockers of a world this frame, in the world's space, before its kinds
		void SetBlockers(uint64_t worldRevision, std::vector<ScatterBlocker> blockers);
		// The parts of a mesh, and the buffer of their indices; none yet while it is being cut
		const std::vector<GrassMeshPart>* GetMeshParts(const std::shared_ptr<const MeshData>& data, uint64_t revision, vk::Buffer& outIndices);
		// After the last kind: tiles nothing needs are let go of, the nearest ones to be made chosen
		void EndKinds();

		// The tiles made this frame, in the frame's own commands, after the meshes' uploads and
		// outside a render pass
		void RecordTiles(vk::raii::CommandBuffer& cmd);

		// The view's blades this frame, outside a render pass, from its world's tiles; drawn inside the
		// scene pass by Draw(). Nothing for a view without grass.
		void RecordBlades(SceneViewGrass& grass, uint64_t worldRevision, const glm::vec3& camera, const Frustum& frustum, vk::DescriptorSet bindlessSet, vk::DescriptorSet viewSet, vk::raii::CommandBuffer& cmd);
		void Draw(const SceneViewGrass& grass, vk::DescriptorSet bindlessSet, vk::DescriptorSet viewSet, vk::raii::CommandBuffer& cmd);
		// After the frame's fence: what the view drew the frame before
		void ReadStats(SceneViewGrass& grass);
		void Release(SceneViewGrass& grass);

		// A blade's triangles, near and far
		static constexpr uint32_t NearTriangles = 13;
		static constexpr uint32_t FarTriangles = 5;

		// For the stats and tests
		struct Stats
		{
			uint32_t Tiles = 0;
			uint32_t TilesMade = 0;
			uint32_t TilesWaiting = 0;
			// Since it began, to tell how much a change made again
			uint64_t TilesMadeInAll = 0;
			uint64_t PointBytes = 0;
			uint64_t TileBytes = 0;
		};
		Stats GetStats() const { return CurrentStats; }

	private:
		// A mesh's blades: the room for its points, and what they were made from: the values that
		// decide them (not how its blades look), the mesh's revision and where it lies
		struct Tile
		{
			std::shared_ptr<const MeshData> Data;
			Mesh* Gpu = nullptr;
			vk::Buffer Indices;
			vk::DeviceSize FirstIndexByte = 0;
			uint32_t Triangles = 0;
			// Of the blockers that reach it, when they were last looked through
			uint64_t BlockersSeen = 0;
			uint64_t BlockersHash = 0;
			uint32_t FirstBlocker = 0;
			uint32_t BlockerCount = 0;
			bool bUseBlockers = true;
			uint64_t MadeFrom = 0;
			uint64_t WantedFrom = 0;
			uint64_t FirstByte = 0;
			uint64_t Bytes = 0;
			uint32_t Capacity = 0;
			uint32_t Slot = 0;
			uint32_t Level = 0;
			uint32_t WantedLevel = 0;
			uint32_t Kind = 0;
			uint64_t WorldRevision = 0;
			float Nearest = 0.0f;
			float Range = 0.0f;
			float PlaceSize = 0.0f;
			glm::vec2 Corner{ 0.0f };
			glm::vec2 Morph{ 0.0f };
			glm::mat4 Model{ 1.0f };
			BoundingBox WorldBounds;
			GrassSettings Settings;
			float ClumpSize = 1.0f;
			float MaxHeight = 0.0f;
			uint64_t LastFrame = 0;
			bool bAllocated = false;
			bool bMade = false;
		};
		struct TileKey
		{
			uint64_t World = 0;
			uint64_t Entity = 0;
			const MeshData* Data = nullptr;
			uint32_t Part = 0;
			bool operator==(const TileKey&) const = default;
		};
		struct TileKeyHash
		{
			size_t operator()(const TileKey& key) const;
		};

		void MakeBuffers();
		void MakeViewBuffers(SceneViewGrass& grass, uint32_t nearCapacity, uint32_t farCapacity);
		void WriteViewSet(SceneViewGrass& grass);
		void FreeTileRange(Tile& tile);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		vk::DescriptorSetLayout BindlessLayout;
		vk::DescriptorSetLayout ViewLayout;

		vk::raii::DescriptorSetLayout TileSetLayout{ nullptr };
		vk::raii::PipelineLayout TilePipelineLayout{ nullptr };
		vk::raii::Pipeline PointsPipeline{ nullptr };
		vk::raii::Pipeline PointsDonePipeline{ nullptr };
		// A set for each tile made in a frame, reset with the frame
		vk::raii::DescriptorPool TileSets{ nullptr };

		vk::raii::DescriptorSetLayout BladeSetLayout{ nullptr };
		vk::raii::PipelineLayout BladePipelineLayout{ nullptr };
		vk::raii::Pipeline BladesPipeline{ nullptr };
		vk::raii::Pipeline BladesDonePipeline{ nullptr };
		vk::raii::Pipeline DrawPipeline{ nullptr };

		vk::raii::Buffer Points{ nullptr };
		vk::raii::DeviceMemory PointsMemory{ nullptr };
		std::unique_ptr<RangeAllocator> PointRanges;
		vk::raii::Buffer TileCounts{ nullptr };
		vk::raii::DeviceMemory TileCountsMemory{ nullptr };
		// The frame's kinds' values; written once a frame
		vk::raii::Buffer KindValues{ nullptr };
		vk::raii::DeviceMemory KindValuesMemory{ nullptr };
		void* KindValuesMapped = nullptr;

		// GrassPoints.slang's Blocker, written for the tiles made in a frame
		vk::raii::Buffer BlockerValues{ nullptr };
		vk::raii::DeviceMemory BlockerValuesMemory{ nullptr };
		void* BlockerValuesMapped = nullptr;

		struct WorldBlockers
		{
			uint64_t Hash = 0;
			std::vector<ScatterBlocker> Blockers;
		};
		std::unordered_map<uint64_t, WorldBlockers> Blockers;

		struct MeshCut
		{
			std::shared_ptr<const MeshData> Data;
			uint64_t Revision = 0;
			uint64_t LastFrame = 0;
			std::vector<GrassMeshPart> Parts;
			std::vector<uint32_t> Indices;
			std::shared_ptr<JobGroup> Job;
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			bool bReady = false;
		};
		std::unordered_map<const MeshData*, std::unique_ptr<MeshCut>> MeshCuts;
		uint64_t TilesMadeInAll = 0;

		std::unordered_map<TileKey, Tile, TileKeyHash> Tiles;
		std::vector<uint32_t> FreeSlots;
		std::vector<Tile*> TilesToMake;
		uint32_t KindCount = 0;
		uint64_t PointsGeneration = 0;
		uint64_t Frame = 0;
		Stats CurrentStats;
		bool bLoggedFull = false;
	};
}

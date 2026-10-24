#pragma once

#include "SceneCulling.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class VulkanContext;
	struct MeshInstances;
	struct MeshInstancesComponent;

	// The GPU copies of the worlds' MeshInstancesComponents, one for each MeshInstances, keyed by
	// the data as RuntimeMeshCache keys meshes: entities that share instances share their buffer.
	// The transforms go up once, when first drawn, and again only when a component that draws them
	// has a higher revision than the one uploaded; instances that no shown world drew in a frame are
	// let go of. Every mesh pipeline reads them as its second vertex stream, a mat4 per instance
	// (GetInstanceBindingDescription()); a draw without instances reads the identity buffer here.
	//
	// Uploaded into new buffers while the worlds are extracted, before the frame records a draw; a
	// buffer replaced or let go of is kept until after the next frame's fence (ReleaseRetired()),
	// as RuntimeMeshCache keeps meshes.
	class MeshInstanceCache
	{
	public:
		// What a draw of the instances needs
		struct Instances
		{
			vk::Buffer Buffer;
			uint32_t Count = 0;
			// To cull by, in the entity's space, for the mesh they were last asked for with
			// (MakeInstanceCullCells())
			std::vector<InstanceCullCell> Cells;
			// Around all of the cells
			BoundingBox Bounds;
			// New with every upload, never 0: tells uploads apart that may get the same handles
			uint64_t UploadId = 0;
		};

		MeshInstanceCache() = default;
		~MeshInstanceCache();
		MeshInstanceCache(const MeshInstanceCache&) = delete;
		MeshInstanceCache& operator=(const MeshInstanceCache&) = delete;

		// The identity buffer, before the first draw
		void Initialize(VulkanContext& context);

		// The instances to draw the component with this frame, uploaded if new or changed, their
		// cells made for a mesh of `meshBounds` (again when the mesh's bounds changed); null when
		// there is nothing to draw: no data, no transforms, or no cell that covers any
		const Instances* Acquire(VulkanContext& context, const MeshInstancesComponent& component, const BoundingBox& meshBounds);
		// One identity transform, for draws without instances
		vk::Buffer GetIdentity() const { return *Identity.Buffer; }

		// After every shown world was extracted: the instances none of them drew this frame are retired
		void EndFrame();
		// After the last frame's fence: what was retired before it is destroyed
		void ReleaseRetired();
		// Everything, at once; only once the GPU is idle
		void Clear();

		size_t GetCount() const { return Entries.size(); }
		// How many uploads there were, for tests
		uint64_t GetUploadCount() const { return UploadCount; }

	private:
		struct GpuBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
		};

		struct Entry
		{
			// Kept alive here, so that no other data is ever made at its address while it is a key
			std::shared_ptr<const MeshInstances> Source;
			std::unique_ptr<GpuBuffer> Gpu;
			Instances Drawn;
			// The revision uploaded, and the mesh bounds the cells were made for
			uint64_t Revision = 0;
			std::optional<BoundingBox> CellsMeshBounds;
			bool bDrawn = false;
		};

		void Upload(VulkanContext& context, Entry& entry, uint64_t revision);
		static std::unique_ptr<GpuBuffer> MakeBuffer(VulkanContext& context, const void* data, size_t size);

		GpuBuffer Identity;
		std::unordered_map<const MeshInstances*, Entry> Entries;
		// Replaced or let go of since the last frame's fence
		std::vector<std::unique_ptr<GpuBuffer>> Retired;
		uint64_t UploadCount = 0;
		uint64_t LastUploadId = 0;
	};
}

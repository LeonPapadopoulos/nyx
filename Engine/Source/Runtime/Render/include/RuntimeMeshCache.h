#pragma once

#include "MeshData.h"
#include "MeshMemoryPool.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class Mesh;
	class VulkanContext;
	struct RuntimeMeshComponent;

	// The GPU meshes of the worlds' RuntimeMeshComponents, one for each MeshData, keyed by the data:
	// the handle that later render proxies and instancing go by, so entities that share a MeshData
	// share its GPU mesh. A mesh is uploaded when first drawn, and again only when a component that
	// draws it has a higher revision than the one uploaded; one that no shown world drew in a frame
	// is let go of. Data that isn't a valid mesh (CheckMeshData()) is drawn as nothing and logged once
	// per revision.
	//
	// Meshes are uploaded into new buffers while the worlds are extracted, before the frame records a
	// draw: the last frame's GPU work is done by then (one frame in flight), and this frame's draws
	// use the new buffers. Their memory is the cache's pool's (MeshMemoryPool), so a mesh costs no
	// allocation of its own, and their data is copied in by the frame's commands (RecordUploads(),
	// before anything draws). A mesh replaced or let go of is kept until after the next frame's fence
	// (ReleaseRetired()), as views let go of are, so it is never destroyed while the GPU may read it.
	class RuntimeMeshCache
	{
	public:
		RuntimeMeshCache() = default;
		~RuntimeMeshCache();
		RuntimeMeshCache(const RuntimeMeshCache&) = delete;
		RuntimeMeshCache& operator=(const RuntimeMeshCache&) = delete;

		// The GPU mesh to draw the component with this frame, uploaded if new or changed; null for no
		// data, an empty mesh or one that isn't valid
		Mesh* Acquire(VulkanContext& context, const RuntimeMeshComponent& component);
		// After every shown world was extracted: the meshes none of them drew this frame are retired
		void EndFrame();
		// The copies of this frame's uploads, into the frame's commands before its first draw
		void RecordUploads(const vk::raii::CommandBuffer& cmd);
		// After the last frame's fence: what was retired before it is destroyed
		void ReleaseRetired();
		// Every mesh, at once; only once the GPU is idle
		void Clear();

		size_t GetMeshCount() const { return Entries.size(); }
		// How many uploads there were, for tests
		uint64_t GetUploadCount() const { return UploadCount; }
		// The pool's blocks and what is in them, and the bytes the last frame copied
		MeshMemoryPool::Stats GetPoolStats() const { return Pool.GetStats(); }
		uint64_t GetBytesLastUploaded() const { return Uploads.GetBytesLastRecorded(); }

	private:
		struct Entry
		{
			// Kept alive here, so that no other data is ever made at its address while it is a key
			std::shared_ptr<const MeshData> Source;
			std::unique_ptr<Mesh> Gpu;
			// The revision uploaded, or found not valid
			uint64_t Revision = 0;
			bool bValid = false;
			bool bDrawn = false;
		};

		void Upload(VulkanContext& context, Entry& entry, uint64_t revision);

		// Before the meshes, so they are destroyed after them: a mesh gives its memory back to the pool
		MeshMemoryPool Pool;
		MeshUploads Uploads;

		std::unordered_map<const MeshData*, Entry> Entries;
		// Replaced or let go of since the last frame's fence
		std::vector<std::unique_ptr<Mesh>> Retired;
		uint64_t UploadCount = 0;
	};
}

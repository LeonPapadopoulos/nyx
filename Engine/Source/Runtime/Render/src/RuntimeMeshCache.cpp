#include "NyxPCH.h"
#include "RuntimeMeshCache.h"

#include "Log.h"
#include "Mesh.h"
#include "RuntimeMeshComponent.h"

namespace Nyx
{
	RuntimeMeshCache::~RuntimeMeshCache() = default;

	Mesh* RuntimeMeshCache::Acquire(VulkanContext& context, const RuntimeMeshComponent& component)
	{
		if (!component.Mesh)
		{
			return nullptr;
		}

		auto found = Entries.find(component.Mesh.get());
		if (found == Entries.end())
		{
			found = Entries.emplace(component.Mesh.get(), Entry{ .Source = component.Mesh }).first;
			Upload(context, found->second, component.Revision);
		}
		else if (component.Revision > found->second.Revision)
		{
			Upload(context, found->second, component.Revision);
		}

		Entry& entry = found->second;
		entry.bDrawn = true;
		return entry.bValid && entry.Gpu && entry.Gpu->GetIndexCount() > 0 ? entry.Gpu.get() : nullptr;
	}

	void RuntimeMeshCache::Upload(VulkanContext& context, Entry& entry, uint64_t revision)
	{
		entry.Revision = revision;
		// Into new buffers: the old ones may be drawn until the GPU is done with them
		if (entry.Gpu)
		{
			Retired.push_back(std::move(entry.Gpu));
		}

		const MeshData& data = *entry.Source;
		const std::string problem = CheckMeshData(data);
		entry.bValid = problem.empty();
		if (!entry.bValid)
		{
			LOG_ERROR("A run-time mesh of {0} vertices and {1} indices isn't drawn: {2}", data.Vertices.size(), data.Indices.size(), problem);
			return;
		}

		entry.Gpu = std::make_unique<Mesh>("RuntimeMesh");
		const MeshBounds bounds = ComputeMeshBounds(data);
		entry.Gpu->SetLocalBounds(bounds.Min, bounds.Max);
		entry.Gpu->UploadPooled(context, data, Pool, Uploads);
		++UploadCount;
	}

	void RuntimeMeshCache::EndFrame()
	{
		for (auto it = Entries.begin(); it != Entries.end();)
		{
			if (it->second.bDrawn)
			{
				it->second.bDrawn = false;
				++it;
				continue;
			}
			if (it->second.Gpu)
			{
				Retired.push_back(std::move(it->second.Gpu));
			}
			it = Entries.erase(it);
		}
	}

	void RuntimeMeshCache::RecordUploads(const vk::raii::CommandBuffer& cmd)
	{
		Uploads.Record(cmd);
	}

	void RuntimeMeshCache::ReleaseRetired()
	{
		Retired.clear();
		Uploads.ReleaseRetired();
	}

	void RuntimeMeshCache::Clear()
	{
		Entries.clear();
		Retired.clear();
		Uploads.Clear();
		Pool.Clear();
	}
}

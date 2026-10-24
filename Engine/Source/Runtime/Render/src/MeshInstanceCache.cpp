#include "NyxPCH.h"
#include "MeshInstanceCache.h"

#include "Log.h"
#include "Mesh.h"
#include "MeshInstancesComponent.h"
#include "VulkanUtil.h"

#include <cstring>

namespace Nyx
{
	namespace
	{
		bool operator==(const BoundingBox& a, const BoundingBox& b)
		{
			return a.Min == b.Min && a.Max == b.Max;
		}
	}

	MeshInstanceCache::~MeshInstanceCache() = default;

	void MeshInstanceCache::Initialize(VulkanContext& context)
	{
		const MeshInstanceData identity{};
		Identity = std::move(*MakeBuffer(context, &identity, sizeof(identity)));
	}

	const MeshInstanceCache::Instances* MeshInstanceCache::Acquire(VulkanContext& context, const MeshInstancesComponent& component, const BoundingBox& meshBounds)
	{
		if (!component.Instances)
		{
			return nullptr;
		}

		auto found = Entries.find(component.Instances.get());
		if (found == Entries.end())
		{
			found = Entries.emplace(component.Instances.get(), Entry{ .Source = component.Instances }).first;
			Upload(context, found->second, component.Revision);
		}
		else if (component.Revision > found->second.Revision)
		{
			Upload(context, found->second, component.Revision);
		}

		Entry& entry = found->second;
		entry.bDrawn = true;
		if (!entry.Gpu)
		{
			return nullptr;
		}

		// The cells hold the mesh at every instance: made again for a mesh of other bounds
		if (!entry.CellsMeshBounds || !(*entry.CellsMeshBounds == meshBounds))
		{
			std::string problem;
			entry.Drawn.Cells = MakeInstanceCullCells(*entry.Source, meshBounds, &problem);
			// Once per upload, not for every mesh drawn with them
			if (!problem.empty() && !entry.CellsMeshBounds)
			{
				LOG_WARNING("Mesh instances: {}; drawn as far as there are transforms", problem);
			}
			entry.CellsMeshBounds = meshBounds;
			entry.Drawn.Bounds = entry.Drawn.Cells.empty() ? BoundingBox{} : entry.Drawn.Cells.front().Bounds;
			for (const InstanceCullCell& cell : entry.Drawn.Cells)
			{
				entry.Drawn.Bounds = UniteBoxes(entry.Drawn.Bounds, cell.Bounds);
			}
		}
		return entry.Drawn.Cells.empty() ? nullptr : &entry.Drawn;
	}

	void MeshInstanceCache::Upload(VulkanContext& context, Entry& entry, uint64_t revision)
	{
		entry.Revision = revision;
		entry.CellsMeshBounds.reset();
		entry.Drawn = {};
		// Into a new buffer: the old one may be drawn until the GPU is done with it
		if (entry.Gpu)
		{
			Retired.push_back(std::move(entry.Gpu));
		}

		const std::vector<glm::mat4>& transforms = entry.Source->Transforms;
		if (transforms.empty())
		{
			return;
		}
		// Each transform with its custom numbers, zero for those that have none
		const std::vector<glm::vec4>& custom = entry.Source->Custom;
		std::vector<MeshInstanceData> stream(transforms.size());
		for (size_t index = 0; index < transforms.size(); ++index)
		{
			stream[index].Transform = transforms[index];
			stream[index].Custom = index < custom.size() ? custom[index] : glm::vec4(0.0f);
		}
		entry.Gpu = MakeBuffer(context, stream.data(), stream.size() * sizeof(MeshInstanceData));
		entry.Drawn.Buffer = *entry.Gpu->Buffer;
		entry.Drawn.Count = static_cast<uint32_t>(transforms.size());
		entry.Drawn.UploadId = ++LastUploadId;
		++UploadCount;
	}

	std::unique_ptr<MeshInstanceCache::GpuBuffer> MeshInstanceCache::MakeBuffer(VulkanContext& context, const void* data, size_t size)
	{
		// Written once by the CPU and read by the GPU, as the meshes' vertices are
		auto buffer = std::make_unique<GpuBuffer>();
		CreateBuffer(context, size, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			buffer->Buffer, buffer->Memory);
		void* mapped = buffer->Memory.mapMemory(0, size);
		std::memcpy(mapped, data, size);
		buffer->Memory.unmapMemory();
		return buffer;
	}

	void MeshInstanceCache::EndFrame()
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

	void MeshInstanceCache::ReleaseRetired()
	{
		Retired.clear();
	}

	void MeshInstanceCache::Clear()
	{
		Entries.clear();
		Retired.clear();
		Identity = {};
	}
}

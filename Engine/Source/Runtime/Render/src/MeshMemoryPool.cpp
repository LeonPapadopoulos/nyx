#include "NyxPCH.h"
#include "MeshMemoryPool.h"

#include "Assertions.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <algorithm>

namespace Nyx
{
	namespace
	{
		uint64_t AlignUp(uint64_t value, uint64_t alignment)
		{
			return alignment > 1 ? (value + alignment - 1) & ~(alignment - 1) : value;
		}

		// The staging buffer's first size; a frame's uploads past it double it
		constexpr vk::DeviceSize FirstStagingSize = 4ull * 1024 * 1024;
	}

	RangeAllocator::RangeAllocator(uint64_t size)
		: Size(size), FreeBytes(size)
	{
		if (size > 0)
		{
			FreeRanges.emplace(0, size);
		}
	}

	std::optional<uint64_t> RangeAllocator::Allocate(uint64_t size, uint64_t alignment)
	{
		if (size == 0)
		{
			return std::nullopt;
		}
		for (auto it = FreeRanges.begin(); it != FreeRanges.end(); ++it)
		{
			const uint64_t start = it->first;
			const uint64_t length = it->second;
			const uint64_t aligned = AlignUp(start, alignment);
			if (aligned + size > start + length)
			{
				continue;
			}
			FreeRanges.erase(it);
			// What is left before the aligned start and after the end stays free
			if (aligned > start)
			{
				FreeRanges.emplace(start, aligned - start);
			}
			if (aligned + size < start + length)
			{
				FreeRanges.emplace(aligned + size, start + length - (aligned + size));
			}
			FreeBytes -= size;
			return aligned;
		}
		return std::nullopt;
	}

	void RangeAllocator::Free(uint64_t offset, uint64_t size)
	{
		ASSERT(size > 0 && offset + size <= Size && "A range given back that the block never had");
		uint64_t start = offset;
		uint64_t end = offset + size;
		// Merged with the free range after it and the one before it, if they touch
		const auto after = FreeRanges.lower_bound(offset);
		if (after != FreeRanges.end() && after->first == end)
		{
			end += after->second;
			FreeRanges.erase(after);
		}
		const auto next = FreeRanges.lower_bound(offset);
		if (next != FreeRanges.begin())
		{
			const auto before = std::prev(next);
			ASSERT(before->first + before->second <= offset && "A range given back twice");
			if (before->first + before->second == offset)
			{
				start = before->first;
				FreeRanges.erase(before);
			}
		}
		FreeRanges.emplace(start, end - start);
		FreeBytes += size;
	}

	MeshMemoryAllocation MeshMemoryPool::Allocate(VulkanContext& context, const vk::MemoryRequirements& requirements)
	{
		const uint32_t memoryType = FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		const uint64_t alignment = std::max<uint64_t>(requirements.alignment, 1);

		auto take = [&](uint32_t index) -> std::optional<MeshMemoryAllocation>
		{
			Block& block = *Blocks[index];
			const std::optional<uint64_t> offset = block.Ranges.Allocate(requirements.size, alignment);
			if (!offset)
			{
				return std::nullopt;
			}
			++block.Allocations;
			return MeshMemoryAllocation{ .Memory = *block.Memory, .Offset = *offset, .Size = requirements.size, .Block = index };
		};

		for (uint32_t index = 0; index < Blocks.size(); ++index)
		{
			if (Blocks[index] && Blocks[index]->MemoryType == memoryType)
			{
				if (const std::optional<MeshMemoryAllocation> allocation = take(index))
				{
					return *allocation;
				}
			}
		}

		auto block = std::make_unique<Block>();
		const vk::DeviceSize size = std::max<vk::DeviceSize>(BlockSize, AlignUp(requirements.size, alignment));
		vk::MemoryAllocateInfo info{};
		info.allocationSize = size;
		info.memoryTypeIndex = memoryType;
		block->Memory = vk::raii::DeviceMemory(context.GetDevice(), info);
		block->MemoryType = memoryType;
		block->Ranges = RangeAllocator(size);

		// Into the first empty slot, or a new one
		uint32_t index = 0;
		while (index < Blocks.size() && Blocks[index])
		{
			++index;
		}
		if (index == Blocks.size())
		{
			Blocks.push_back(nullptr);
		}
		Blocks[index] = std::move(block);
		const std::optional<MeshMemoryAllocation> allocation = take(index);
		ASSERT(allocation && "A new block can't hold the buffer it was made for");
		return allocation.value_or(MeshMemoryAllocation{});
	}

	void MeshMemoryPool::Free(const MeshMemoryAllocation& allocation)
	{
		if (!allocation.IsValid() || allocation.Block >= Blocks.size() || !Blocks[allocation.Block])
		{
			return;
		}
		Block& block = *Blocks[allocation.Block];
		block.Ranges.Free(allocation.Offset, allocation.Size);
		--block.Allocations;
		if (block.Allocations > 0)
		{
			return;
		}

		// An empty block goes, unless it is the last of its memory type
		const bool bOthersOfItsType = std::any_of(Blocks.begin(), Blocks.end(), [&](const std::unique_ptr<Block>& other)
			{ return other && other.get() != &block && other->MemoryType == block.MemoryType; });
		if (bOthersOfItsType)
		{
			Blocks[allocation.Block].reset();
		}
	}

	void MeshMemoryPool::Clear()
	{
		Blocks.clear();
	}

	MeshMemoryPool::Stats MeshMemoryPool::GetStats() const
	{
		Stats stats;
		for (const std::unique_ptr<Block>& block : Blocks)
		{
			if (!block)
			{
				continue;
			}
			++stats.Blocks;
			stats.BlockBytes += block->Ranges.GetSize();
			stats.UsedBytes += block->Ranges.GetSize() - block->Ranges.GetFreeBytes();
			stats.Allocations += block->Allocations;
		}
		return stats;
	}

	void* MeshUploads::Reserve(VulkanContext& context, vk::DeviceSize size, vk::Buffer destination)
	{
		// Each copy starts on a multiple of 16 bytes, which every vertex and index type divides
		const vk::DeviceSize start = AlignUp(Used, 16);
		if (start + size > Current.Capacity)
		{
			if (Current.Capacity > 0)
			{
				Retired.push_back(std::move(Current));
				Current = Staging{};
			}
			Staging staging;
			staging.Capacity = std::max<vk::DeviceSize>(std::max(FirstStagingSize, Retired.empty() ? FirstStagingSize : Retired.back().Capacity * 2),
				AlignUp(size, 16));
			CreateBuffer(context, staging.Capacity, vk::BufferUsageFlagBits::eTransferSrc,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging.Buffer, staging.Memory);
			staging.Mapped = staging.Memory.mapMemory(0, staging.Capacity);
			Current = std::move(staging);
			Used = 0;
			return Reserve(context, size, destination);
		}

		Pending.push_back(Copy{ .Source = *Current.Buffer, .SourceOffset = start, .Destination = destination, .Size = size });
		Used = start + size;
		return static_cast<std::byte*>(Current.Mapped) + start;
	}

	void MeshUploads::Record(const vk::raii::CommandBuffer& cmd)
	{
		if (Pending.empty())
		{
			return;
		}
		BytesLastRecorded = 0;
		for (const Copy& copy : Pending)
		{
			cmd.copyBuffer(copy.Source, copy.Destination, vk::BufferCopy{ copy.SourceOffset, 0, copy.Size });
			BytesLastRecorded += copy.Size;
		}
		Pending.clear();

		vk::MemoryBarrier barrier{};
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eVertexInput, {}, barrier, {}, {});
	}

	void MeshUploads::ReleaseRetired()
	{
		ASSERT(Pending.empty() && "Mesh uploads were reserved and never recorded");
		Retired.clear();
		Used = 0;
	}

	void MeshUploads::Clear()
	{
		Pending.clear();
		Retired.clear();
		Current = Staging{};
		Used = 0;
	}
}

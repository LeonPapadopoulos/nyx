#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace Nyx
{
	class VulkanContext;

	// The free ranges of a block of `size` bytes: a range is taken from the first free one it fits in,
	// at its alignment, and merged with its neighbours again when freed. Arithmetic only, so the
	// tests can check it without a GPU.
	class RangeAllocator
	{
	public:
		explicit RangeAllocator(uint64_t size);

		// Where `size` bytes start, at a multiple of `alignment` (a power of two); none if no free
		// range holds them
		std::optional<uint64_t> Allocate(uint64_t size, uint64_t alignment);
		// What Allocate() gave, given back: exactly the offset and size it was asked for
		void Free(uint64_t offset, uint64_t size);

		uint64_t GetSize() const { return Size; }
		uint64_t GetFreeBytes() const { return FreeBytes; }
		size_t GetFreeRangeCount() const { return FreeRanges.size(); }
		bool IsEmpty() const { return FreeBytes == Size; }

	private:
		// Start to size, never touching: neighbours are merged
		std::map<uint64_t, uint64_t> FreeRanges;
		uint64_t Size = 0;
		uint64_t FreeBytes = 0;
	};

	// Part of a block of the pool's memory
	struct MeshMemoryAllocation
	{
		vk::DeviceMemory Memory;
		vk::DeviceSize Offset = 0;
		vk::DeviceSize Size = 0;
		uint32_t Block = UINT32_MAX;

		bool IsValid() const { return Block != UINT32_MAX; }
	};

	// GPU memory for many meshes' buffers in a few large blocks, so that a mesh costs no allocation of
	// its own: drivers allow few (often 4096), and a world of terrain chunks and scatters has
	// thousands of meshes. Device-local: the GPU reads it fastest, and it is filled by copies
	// (MeshUploads). Each buffer is bound to its part of a block; a block that empties goes, but the
	// last of its memory type stays for the next meshes.
	class MeshMemoryPool
	{
	public:
		// A new block's size, unless one buffer needs more
		static constexpr vk::DeviceSize BlockSize = 64ull * 1024 * 1024;

		MeshMemoryPool() = default;
		MeshMemoryPool(const MeshMemoryPool&) = delete;
		MeshMemoryPool& operator=(const MeshMemoryPool&) = delete;

		// Memory for a buffer with these requirements; a new block if no block of a memory type it
		// allows has room
		MeshMemoryAllocation Allocate(VulkanContext& context, const vk::MemoryRequirements& requirements);
		// Only once the GPU is done with the buffer bound to it, and after the buffer is destroyed
		void Free(const MeshMemoryAllocation& allocation);
		// Every block; only once nothing is bound to them
		void Clear();

		struct Stats
		{
			uint32_t Blocks = 0;
			uint64_t BlockBytes = 0;
			uint64_t UsedBytes = 0;
			uint32_t Allocations = 0;
		};
		Stats GetStats() const;

	private:
		struct Block
		{
			vk::raii::DeviceMemory Memory{ nullptr };
			uint32_t MemoryType = 0;
			RangeAllocator Ranges{ 0 };
			uint32_t Allocations = 0;
		};

		// By index, so an allocation names its block; a block let go of leaves an empty slot
		std::vector<std::unique_ptr<Block>> Blocks;
	};

	// The copies of a frame into the pool's memory: each mesh's data is written into a staging buffer
	// the CPU sees, and Record() copies it into the mesh's buffers with the frame's commands, before
	// anything is drawn. The staging buffer is used again from its start once the frame it was
	// written in is done (ReleaseRetired()); one too small for a frame's uploads is replaced by one
	// twice as large, the old one kept until then.
	class MeshUploads
	{
	public:
		MeshUploads() = default;
		MeshUploads(const MeshUploads&) = delete;
		MeshUploads& operator=(const MeshUploads&) = delete;

		// Where to write `size` bytes that this frame copies into `destination`, from its start
		void* Reserve(VulkanContext& context, vk::DeviceSize size, vk::Buffer destination);
		// The reserved copies into the command buffer, and a barrier that makes them visible to
		// vertex and index reads
		void Record(const vk::raii::CommandBuffer& cmd);
		// After the frame's fence: the staging buffer is free again
		void ReleaseRetired();
		// Every buffer; only once the GPU is idle
		void Clear();

		// Bytes copied by the last frame that recorded copies, for the stats and the tests
		uint64_t GetBytesLastRecorded() const { return BytesLastRecorded; }

	private:
		struct Copy
		{
			vk::Buffer Source;
			vk::DeviceSize SourceOffset = 0;
			vk::Buffer Destination;
			vk::DeviceSize Size = 0;
		};

		struct Staging
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			void* Mapped = nullptr;
			vk::DeviceSize Capacity = 0;
		};

		Staging Current;
		vk::DeviceSize Used = 0;
		// Outgrown this frame: copies may still read them
		std::vector<Staging> Retired;
		std::vector<Copy> Pending;
		uint64_t BytesLastRecorded = 0;
	};
}

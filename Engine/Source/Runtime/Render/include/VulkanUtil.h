#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>

namespace Nyx
{
	class VulkanContext;

	// The first memory type that `typeFilter` allows (one bit per type, as in
	// vk::MemoryRequirements) and that has all of `properties`. Throws when there is none.
	uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

	// A buffer of `size` bytes with memory of its own, bound at its start
	void CreateBuffer(
		VulkanContext& context,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::raii::Buffer& outBuffer,
		vk::raii::DeviceMemory& outMemory);

	// A command buffer from the graphics pool, begun for one submission. EndSingleTimeCommands()
	// submits it and waits until the GPU has run it.
	vk::raii::CommandBuffer BeginSingleTimeCommands(VulkanContext& context);
	void EndSingleTimeCommands(VulkanContext& context, vk::raii::CommandBuffer&& commandBuffer);

	// Records a barrier that moves `range` of the image from `oldLayout` to `newLayout`: what
	// `srcStages` did with `srcAccess` happens before what `dstStages` does with `dstAccess`
	void TransitionImageLayout(
		vk::CommandBuffer commandBuffer,
		vk::Image image,
		const vk::ImageSubresourceRange& range,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout,
		vk::PipelineStageFlags srcStages,
		vk::AccessFlags srcAccess,
		vk::PipelineStageFlags dstStages,
		vk::AccessFlags dstAccess);
}

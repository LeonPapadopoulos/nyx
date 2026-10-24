#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Nyx
{
	class VulkanContext;

	// The depth tests for what is nearer: depth is reversed (NearPlaneDepth in CameraProjection.h),
	// so nearer is greater, and depth buffers are cleared to FarPlaneDepth
	constexpr vk::CompareOp DepthTestNearer = vk::CompareOp::eGreater;
	constexpr vk::CompareOp DepthTestNearerOrEqual = vk::CompareOp::eGreaterOrEqual;

	// The dynamic states of every pipeline that draws meshes (materials, the editor's picking and
	// selection masks): the viewport, the scissor, and which faces are drawn (SetMeshFaces())
	constexpr std::array<vk::DynamicState, 4> MeshDynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eCullMode,
		vk::DynamicState::eFrontFace };

	// Which faces of a mesh's triangles the next draws draw: the front, counter-clockwise as the
	// camera sees it (every mesh Nyx makes and glTF's), or both for a two-sided material. A
	// transform that mirrors (whose determinant is negative) turns the winding round, so the
	// front is clockwise then.
	void SetMeshFaces(const vk::raii::CommandBuffer& cmd, bool bTwoSided, bool bMirrored);

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

	// A shader module of the SPIR-V code
	vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, const std::vector<uint32_t>& spirv);

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

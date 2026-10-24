#include "NyxPCH.h"
#include "VulkanUtil.h"

#include "VulkanContext.h"

#include <stdexcept>

namespace Nyx
{
	void SetMeshFaces(const vk::raii::CommandBuffer& cmd, bool bTwoSided, bool bMirrored)
	{
		cmd.setCullMode(bTwoSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
		cmd.setFrontFace(bMirrored ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise);
	}

	uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
		for (uint32_t typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex)
		{
			if ((typeFilter & (1u << typeIndex)) && (memoryProperties.memoryTypes[typeIndex].propertyFlags & properties) == properties)
			{
				return typeIndex;
			}
		}
		throw std::runtime_error("Failed to find suitable Vulkan memory type.");
	}

	void CreateBuffer(
		VulkanContext& context,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::raii::Buffer& outBuffer,
		vk::raii::DeviceMemory& outMemory)
	{
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		outBuffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

		const vk::MemoryRequirements requirements = outBuffer.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, properties);
		outMemory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		outBuffer.bindMemory(*outMemory, 0);
	}

	vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, const std::vector<uint32_t>& spirv)
	{
		vk::ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = spirv.size() * sizeof(uint32_t);
		createInfo.pCode = spirv.data();

		return vk::raii::ShaderModule(context.GetDevice(), createInfo);
	}

	vk::raii::CommandBuffer BeginSingleTimeCommands(VulkanContext& context)
	{
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *context.GetGraphicsCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		vk::raii::CommandBuffers commandBuffers(context.GetDevice(), allocInfo);
		vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers.front());

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		commandBuffer.begin(beginInfo);
		return commandBuffer;
	}

	void EndSingleTimeCommands(VulkanContext& context, vk::raii::CommandBuffer&& commandBuffer)
	{
		commandBuffer.end();

		const vk::CommandBuffer rawCommandBuffer = *commandBuffer;
		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &rawCommandBuffer;

		context.GetGraphicsQueue().submit(submitInfo);
		context.GetGraphicsQueue().waitIdle();
	}

	void TransitionImageLayout(
		vk::CommandBuffer commandBuffer,
		vk::Image image,
		const vk::ImageSubresourceRange& range,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout,
		vk::PipelineStageFlags srcStages,
		vk::AccessFlags srcAccess,
		vk::PipelineStageFlags dstStages,
		vk::AccessFlags dstAccess)
	{
		vk::ImageMemoryBarrier barrier{};
		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = range;
		commandBuffer.pipelineBarrier(srcStages, dstStages, {}, {}, {}, barrier);
	}
}

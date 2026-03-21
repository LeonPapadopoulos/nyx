#include "NyxPCH.h"
#include "OffscreenRenderTarget.h"
#include "VulkanContext.h"

namespace Nyx
{
	static uint32_t FindMemoryType(
		vk::PhysicalDevice physicalDevice,
		uint32_t typeFilter,
		vk::MemoryPropertyFlags properties)
	{
		vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((typeFilter & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type.");
	}

	void OffscreenRenderTarget::Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format)
	{
		Extent = vk::Extent2D{ width, height };
		Format = format;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(width, height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;

		Image = vk::raii::Image(context.GetDevice(), imageInfo);

		vk::MemoryRequirements memRequirements = Image.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		Memory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		Image.bindMemory(*Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = Format;
		viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		ImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);
	}

	void OffscreenRenderTarget::Shutdown()
	{
		ImageView = nullptr;
		Memory = nullptr;
		Image = nullptr;
	}

	void OffscreenRenderTarget::Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format)
	{
		Shutdown();
		Initialize(context, width, height, format);
	}
}
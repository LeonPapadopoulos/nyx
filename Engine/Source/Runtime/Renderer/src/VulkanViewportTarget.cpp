#include "NyxPCH.h"
#include "VulkanViewportTarget.h"
#include "VulkanContext.h"

#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Nyx
{
	void VulkanViewportTarget::Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format)
	{
		Extent = vk::Extent2D{ std::max(1u, width), std::max(1u, height) };
		Format = format;

		CreateImage(context);
		CreateImageView(context);
		CreateSampler(context);
		CreateRenderPass(context);
		CreateFramebuffer(context);
		RegisterImGuiTexture();
	}

	void VulkanViewportTarget::Shutdown(VulkanContext& context)
	{
		context.GetDevice().waitIdle();

		DestroyImGuiTexture();

		Framebuffer = nullptr;
		RenderPass = nullptr;
		Sampler = nullptr;
		ImageView = nullptr;
		Memory = nullptr;
		Image = nullptr;

		Extent = vk::Extent2D{ 0, 0 };
		Format = vk::Format::eUndefined;
	}

	void VulkanViewportTarget::EnsureSize(VulkanContext& context, uint32_t width, uint32_t height)
	{
		width = std::max(1u, width);
		height = std::max(1u, height);

		if (Extent.width == width && Extent.height == height)
		{
			return;
		}

		Recreate(context, width, height, Format);
	}

	void VulkanViewportTarget::Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format)
	{
		Shutdown(context);
		Initialize(context, width, height, format);
	}

	void VulkanViewportTarget::BeginRenderPass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearValue& clearValue)
	{
		vk::RenderPassBeginInfo rpInfo{};
		rpInfo.renderPass = *RenderPass;
		rpInfo.framebuffer = *Framebuffer;
		rpInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpInfo.renderArea.extent = Extent;
		rpInfo.clearValueCount = 1;
		rpInfo.pClearValues = &clearValue;

		commandBuffer.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
	}

	void VulkanViewportTarget::EndRenderPass(vk::raii::CommandBuffer& commandBuffer)
	{
		commandBuffer.endRenderPass();
	}

	void VulkanViewportTarget::CreateImage(VulkanContext& context)
	{
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(Extent.width, Extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;

		Image = vk::raii::Image(context.GetDevice(), imageInfo);

		const vk::MemoryRequirements memRequirements = Image.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		Memory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		Image.bindMemory(*Memory, 0);
	}

	void VulkanViewportTarget::CreateImageView(VulkanContext& context)
	{
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

	void VulkanViewportTarget::CreateSampler(VulkanContext& context)
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		Sampler = vk::raii::Sampler(context.GetDevice(), samplerInfo);
	}

	void VulkanViewportTarget::CreateRenderPass(VulkanContext& context)
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = Format;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dependency{};
		dependency.srcSubpass = 0;
		dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		RenderPass = vk::raii::RenderPass(context.GetDevice(), renderPassInfo);
	}

	void VulkanViewportTarget::CreateFramebuffer(VulkanContext& context)
	{
		vk::ImageView attachments[] = { *ImageView };

		vk::FramebufferCreateInfo fbInfo{};
		fbInfo.renderPass = *RenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = attachments;
		fbInfo.width = Extent.width;
		fbInfo.height = Extent.height;
		fbInfo.layers = 1;

		Framebuffer = vk::raii::Framebuffer(context.GetDevice(), fbInfo);
	}

	void VulkanViewportTarget::RegisterImGuiTexture()
	{
		ImGuiTextureId = reinterpret_cast<ImTextureID>(
			ImGui_ImplVulkan_AddTexture(
				*Sampler,
				*ImageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	}

	void VulkanViewportTarget::DestroyImGuiTexture()
	{
		if (ImGuiTextureId)
		{
			ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(ImGuiTextureId));
			ImGuiTextureId = ImTextureID{};
		}
	}

	uint32_t VulkanViewportTarget::FindMemoryType(
		vk::PhysicalDevice physicalDevice,
		uint32_t typeFilter,
		vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type.");
	}
}
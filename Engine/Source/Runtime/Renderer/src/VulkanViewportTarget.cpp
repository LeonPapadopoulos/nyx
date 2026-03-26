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

		CreateDepthResources(context);

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

	void VulkanViewportTarget::BeginRenderPass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue, float clearDepth)
	{
		std::array<vk::ClearValue, 2> clearValues;
		clearValues[0].color = clearValue;
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(clearDepth, 0);

		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = *RenderPass;
		beginInfo.framebuffer = *Framebuffer;
		beginInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		beginInfo.renderArea.extent = Extent;
		beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		beginInfo.pClearValues = clearValues.data();

		commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport(
			0.0f,
			0.0f,
			static_cast<float>(Extent.width),
			static_cast<float>(Extent.height),
			0.0f,
			1.0f);

		vk::Rect2D scissor({ 0, 0 }, Extent);

		commandBuffer.setViewport(0, viewport);
		commandBuffer.setScissor(0, scissor);
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

		vk::AttachmentDescription depthAttachment{};
		depthAttachment.format = DepthFormat;
		depthAttachment.samples = vk::SampleCountFlagBits::e1;
		depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
		depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthRef{};
		depthRef.attachment = 1;
		depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		std::array<vk::AttachmentDescription, 2> attachments =
		{
			colorAttachment,
			depthAttachment
		};

		vk::SubpassDependency deps[2]{};

		// External -> subpass
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		deps[0].dstStageMask =
			vk::PipelineStageFlagBits::eColorAttachmentOutput |
			vk::PipelineStageFlagBits::eEarlyFragmentTests;
		deps[0].srcAccessMask = {};
		deps[0].dstAccessMask =
			vk::AccessFlagBits::eColorAttachmentWrite |
			vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		// Subpass -> external
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		deps[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		deps[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		deps[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = deps;

		RenderPass = vk::raii::RenderPass(context.GetDevice(), renderPassInfo);
	}

	void VulkanViewportTarget::CreateFramebuffer(VulkanContext& context)
	{
		std::array<vk::ImageView, 2> attachments =
		{
			*ImageView,
			*DepthImageView
		};

		vk::FramebufferCreateInfo fbInfo{};
		fbInfo.renderPass = *RenderPass;
		fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbInfo.pAttachments = attachments.data();
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

	void VulkanViewportTarget::CreateDepthResources(VulkanContext& context)
	{
		DepthFormat = FindDepthFormat(*context.GetPhysicalDevice());
		CreateDepthImage(context);
		CreateDepthImageView(context);
	}

	void VulkanViewportTarget::CreateDepthImage(VulkanContext& context)
	{
		vk::ImageCreateInfo imageInfo(
			{},
			vk::ImageType::e2D,
			DepthFormat,
			vk::Extent3D(Extent.width, Extent.height, 1),
			1,
			1,
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::SharingMode::eExclusive,
			{},
			vk::ImageLayout::eUndefined);

		DepthImage = vk::raii::Image(context.GetDevice(), imageInfo);

		vk::MemoryRequirements memRequirements = DepthImage.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo(
			memRequirements.size,
			FindMemoryType(
				*context.GetPhysicalDevice(),
				memRequirements.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eDeviceLocal));

		DepthMemory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		DepthImage.bindMemory(*DepthMemory, 0);
	}

	void VulkanViewportTarget::CreateDepthImageView(VulkanContext& context)
	{
		vk::ImageViewCreateInfo viewInfo(
			{},
			*DepthImage,
			vk::ImageViewType::e2D,
			DepthFormat,
			{},
			vk::ImageSubresourceRange(
				vk::ImageAspectFlagBits::eDepth,
				0, 1,
				0, 1));

		DepthImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);
	}

	vk::Format VulkanViewportTarget::FindSupportedFormat(vk::PhysicalDevice physicalDevice, const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
	{
		for (vk::Format format : candidates)
		{
			vk::FormatProperties props = physicalDevice.getFormatProperties(format);

			if (tiling == vk::ImageTiling::eLinear &&
				(props.linearTilingFeatures & features) == features)
			{
				return format;
			}

			if (tiling == vk::ImageTiling::eOptimal &&
				(props.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		throw std::runtime_error("Failed to find supported Vulkan format.");
	}

	vk::Format VulkanViewportTarget::FindDepthFormat(vk::PhysicalDevice physicalDevice)
	{
		return FindSupportedFormat(
			physicalDevice,
			{
				vk::Format::eD32Sfloat,
				vk::Format::eD32SfloatS8Uint,
				vk::Format::eD24UnormS8Uint
			},
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment);
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
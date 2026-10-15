#include "NyxPCH.h"
#include "VulkanViewportTarget.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace Nyx
{
	void VulkanViewportTarget::Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format)
	{
		Extent = vk::Extent2D{ std::max(1u, width), std::max(1u, height) };
		Format = format;

		CreateImage(context);
		CreateImageView(context);
		CreateHdrImage(context);
		CreateSampler(context);

		CreateDepthResources(context);

		CreateRenderPass(context);
		CreateFramebuffer(context);
		RegisterImGuiTexture();
	}

	void VulkanViewportTarget::Shutdown(VulkanContext& context)
	{
		context.WaitIdle();

		DestroyImGuiTexture();

		Framebuffer = nullptr;
		RenderPass = nullptr;
		SceneFramebuffer = nullptr;
		SceneRenderPass = nullptr;
		HdrImageView = nullptr;
		HdrMemory = nullptr;
		HdrImage = nullptr;
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

	void VulkanViewportTarget::BeginScenePass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue, float clearDepth)
	{
		std::vector<vk::ClearValue> clearValues(2);
		clearValues[0].color = clearValue;
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(clearDepth, 0);
		BeginPass(commandBuffer, *SceneRenderPass, *SceneFramebuffer, clearValues);
	}

	void VulkanViewportTarget::BeginPicturePass(vk::raii::CommandBuffer& commandBuffer)
	{
		BeginPass(commandBuffer, *RenderPass, *Framebuffer, {});
	}

	void VulkanViewportTarget::BeginPass(vk::raii::CommandBuffer& commandBuffer, vk::RenderPass renderPass, vk::Framebuffer framebuffer, const std::vector<vk::ClearValue>& clearValues)
	{
		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = renderPass;
		beginInfo.framebuffer = framebuffer;
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
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
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

	void VulkanViewportTarget::CreateHdrImage(VulkanContext& context)
	{
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(Extent.width, Extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = HdrFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		HdrImage = vk::raii::Image(context.GetDevice(), imageInfo);

		const vk::MemoryRequirements memRequirements = HdrImage.getMemoryRequirements();
		HdrMemory = vk::raii::DeviceMemory(context.GetDevice(), vk::MemoryAllocateInfo(memRequirements.size,
			FindMemoryType(*context.GetPhysicalDevice(), memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		HdrImage.bindMemory(*HdrMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *HdrImage;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = HdrFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		HdrImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);
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
		const vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
		const vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		// The scene: HDR colour and depth, cleared, both kept for the picture pass
		{
			vk::AttachmentDescription color{};
			color.format = HdrFormat;
			color.samples = vk::SampleCountFlagBits::e1;
			color.loadOp = vk::AttachmentLoadOp::eClear;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = vk::ImageLayout::eUndefined;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentDescription depth = color;
			depth.format = DepthFormat;
			depth.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			std::array<vk::SubpassDependency, 2> dependencies{};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
			dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
			dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

			// The picture pass samples the colour and tests against the depth
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
			dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

			const std::array<vk::AttachmentDescription, 2> attachments = { color, depth };
			SceneRenderPass = vk::raii::RenderPass(context.GetDevice(), vk::RenderPassCreateInfo({}, attachments, subpass, dependencies));
		}

		// The picture: written whole by the tonemap, so not loaded; the scene's depth loaded
		{
			vk::AttachmentDescription color{};
			color.format = Format;
			color.samples = vk::SampleCountFlagBits::e1;
			color.loadOp = vk::AttachmentLoadOp::eDontCare;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = vk::ImageLayout::eUndefined;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentDescription depth = color;
			depth.format = DepthFormat;
			depth.loadOp = vk::AttachmentLoadOp::eLoad;
			depth.storeOp = vk::AttachmentStoreOp::eDontCare;
			depth.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			depth.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			std::array<vk::SubpassDependency, 2> dependencies{};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
			dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eFragmentShader;
			dependencies[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eShaderRead;

			// The picture is shown and blurred afterwards
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
			dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

			const std::array<vk::AttachmentDescription, 2> attachments = { color, depth };
			RenderPass = vk::raii::RenderPass(context.GetDevice(), vk::RenderPassCreateInfo({}, attachments, subpass, dependencies));
		}
	}

	void VulkanViewportTarget::CreateFramebuffer(VulkanContext& context)
	{
		std::array<vk::ImageView, 2> attachments = {
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

		const std::array<vk::ImageView, 2> sceneAttachments = { *HdrImageView, *DepthImageView };
		fbInfo.renderPass = *SceneRenderPass;
		fbInfo.pAttachments = sceneAttachments.data();
		SceneFramebuffer = vk::raii::Framebuffer(context.GetDevice(), fbInfo);
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
			{ vk::Format::eD32Sfloat,
				vk::Format::eD32SfloatS8Uint,
				vk::Format::eD24UnormS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment);
	}

}
#include "NyxPCH.h"
#include "VulkanGlass.h"

#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <array>
#include <stdexcept>

namespace Nyx
{
	namespace
	{
		// Glass blurs what lies behind it a little less than the scene views' glow does: halved
		// three times, to 1/8, so a little of the shapes behind still shows through.
		const BlurOptions GlassBlur{ 3, 1 };

	}

	void VulkanGlassBackdrop::Record(vk::raii::CommandBuffer& commandBuffer, VulkanContext& context, VulkanBlur& blur, vk::Format format, vk::Extent2D extent,
		const std::function<void(vk::CommandBuffer)>& draw)
	{
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}
		// The previous frame has finished with the old image by now (InFlightFence).
		if (!*RenderPass || Format != format || Extent != extent)
		{
			Create(context, format, extent);
		}

		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = *RenderPass;
		beginInfo.framebuffer = *Framebuffer;
		beginInfo.renderArea = vk::Rect2D({ 0, 0 }, Extent);
		beginInfo.clearValueCount = 1;
		beginInfo.pClearValues = &clearValue;
		commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
		draw(*commandBuffer);
		commandBuffer.endRenderPass();

		// Drawn, then sampled by the blur
		TransitionImageLayout(*commandBuffer, *Image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1),
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		blur.Blur(commandBuffer, *View, Extent, Blurred, GlassBlur);
	}

	void VulkanGlassBackdrop::Release()
	{
		Blurred.Release();
		Framebuffer = nullptr;
		View = nullptr;
		Image = nullptr;
		Memory = nullptr;
		RenderPass = nullptr;
		Format = vk::Format::eUndefined;
		Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanGlassBackdrop::Create(VulkanContext& context, vk::Format format, vk::Extent2D extent)
	{
		Release();
		vk::raii::Device& device = context.GetDevice();
		Format = format;
		Extent = extent;

		// Cleared, then left for the barrier in Record() to make ready to sample
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = format;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		// Exactly the swapchain's one dependency (VulkanSwapchain::CreateRenderPass()): a pipeline
		// only draws in render passes compatible with the one it was made for, and that takes
		// the same dependencies as well as the same format.
		vk::SubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.srcAccessMask = {};
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;
		RenderPass = vk::raii::RenderPass(device, renderPassInfo);

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = Image.getMemoryRequirements();
		Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size,
			FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		Image.bindMemory(*Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		View = vk::raii::ImageView(device, viewInfo);

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = *RenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &*View;
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		Framebuffer = vk::raii::Framebuffer(device, framebufferInfo);
	}
}

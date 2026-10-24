#include "NyxPCH.h"
#include "VulkanViewportTarget.h"
#include "CameraProjection.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace Nyx
{
	void VulkanViewportTarget::Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::SampleCountFlagBits samples)
	{
		Extent = vk::Extent2D{ std::max(1u, width), std::max(1u, height) };
		Format = format;
		Samples = samples;
		MemorySize = 0;

		CreateImage(context);
		CreateImageView(context);
		CreateHdrImage(context);
		CreateSampler(context);

		CreateDepthResources(context);

		if (Samples != vk::SampleCountFlagBits::e1)
		{
			// Drawn into and resolved, never read afterwards: transient, which a tiled GPU keeps on chip
			constexpr vk::ImageUsageFlags transient = vk::ImageUsageFlagBits::eTransientAttachment;
			CreateMultisampledImage(context, HdrFormat, vk::ImageUsageFlagBits::eColorAttachment | transient, vk::ImageAspectFlagBits::eColor, MultisampledHdrImage,
				MultisampledHdrMemory, MultisampledHdrImageView);
			CreateMultisampledImage(context, AmbientFormat, vk::ImageUsageFlagBits::eColorAttachment | transient, vk::ImageAspectFlagBits::eColor, MultisampledAmbientImage,
				MultisampledAmbientMemory, MultisampledAmbientImageView);
			CreateMultisampledImage(context, DepthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment | transient, vk::ImageAspectFlagBits::eDepth, MultisampledDepthImage,
				MultisampledDepthMemory, MultisampledDepthImageView);

			const auto properties = context.GetPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDepthStencilResolveProperties>();
			DepthResolveMode = ChooseDepthResolveMode(properties.get<vk::PhysicalDeviceDepthStencilResolveProperties>().supportedDepthResolveModes);
		}

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
		MultisampledDepthImageView = nullptr;
		MultisampledDepthMemory = nullptr;
		MultisampledDepthImage = nullptr;
		MultisampledAmbientImageView = nullptr;
		MultisampledAmbientMemory = nullptr;
		MultisampledAmbientImage = nullptr;
		MultisampledHdrImageView = nullptr;
		MultisampledHdrMemory = nullptr;
		MultisampledHdrImage = nullptr;
		AmbientImageView = nullptr;
		AmbientMemory = nullptr;
		AmbientImage = nullptr;
		HdrImageView = nullptr;
		HdrMemory = nullptr;
		HdrImage = nullptr;
		Sampler = nullptr;
		ImageView = nullptr;
		Memory = nullptr;
		Image = nullptr;

		Extent = vk::Extent2D{ 0, 0 };
		Format = vk::Format::eUndefined;
		MemorySize = 0;
	}

	void VulkanViewportTarget::EnsureSize(VulkanContext& context, uint32_t width, uint32_t height)
	{
		width = std::max(1u, width);
		height = std::max(1u, height);

		if (Extent.width == width && Extent.height == height)
		{
			return;
		}

		Recreate(context, width, height, Format, Samples);
	}

	void VulkanViewportTarget::Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::SampleCountFlagBits samples)
	{
		Shutdown(context);
		Initialize(context, width, height, format, samples);
	}

	vk::ResolveModeFlagBits VulkanViewportTarget::ChooseDepthResolveMode(vk::ResolveModeFlags supported)
	{
		return (supported & vk::ResolveModeFlagBits::eMax) ? vk::ResolveModeFlagBits::eMax : vk::ResolveModeFlagBits::eSampleZero;
	}

	void VulkanViewportTarget::BeginScenePass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue)
	{
		// The first three attachments, multisampled or not: the targets drawn into
		std::vector<vk::ClearValue> clearValues(3);
		clearValues[0].color = clearValue;
		clearValues[1].color = vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[2].depthStencil = vk::ClearDepthStencilValue(FarPlaneDepth, 0);
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
		Memory = AllocateImageMemory(context, Image);
	}

	vk::raii::DeviceMemory VulkanViewportTarget::AllocateImageMemory(VulkanContext& context, const vk::raii::Image& image)
	{
		const vk::MemoryRequirements requirements = image.getMemoryRequirements();
		vk::raii::DeviceMemory memory(context.GetDevice(),
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		image.bindMemory(*memory, 0);
		MemorySize += requirements.size;
		return memory;
	}

	void VulkanViewportTarget::CreateMultisampledImage(VulkanContext& context, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect, vk::raii::Image& outImage,
		vk::raii::DeviceMemory& outMemory, vk::raii::ImageView& outView)
	{
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(Extent.width, Extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = usage;
		imageInfo.samples = Samples;
		outImage = vk::raii::Image(context.GetDevice(), imageInfo);
		outMemory = AllocateImageMemory(context, outImage);
		outView = vk::raii::ImageView(context.GetDevice(), vk::ImageViewCreateInfo({}, *outImage, vk::ImageViewType::e2D, format, {}, vk::ImageSubresourceRange(aspect, 0, 1, 0, 1)));
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
		HdrMemory = AllocateImageMemory(context, HdrImage);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *HdrImage;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = HdrFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		HdrImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);

		// The sky's light apart, as big
		imageInfo.format = AmbientFormat;
		AmbientImage = vk::raii::Image(context.GetDevice(), imageInfo);
		AmbientMemory = AllocateImageMemory(context, AmbientImage);
		viewInfo.image = *AmbientImage;
		viewInfo.format = AmbientFormat;
		AmbientImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);
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

		// The scene: HDR colour, the ambient light and depth, cleared, all kept for the passes after
		if (Samples != vk::SampleCountFlagBits::e1)
		{
			CreateMultisampledScenePass(context);
		}
		else
		{
			const std::array<vk::AttachmentReference, 2> sceneColorRefs = { vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
				vk::AttachmentReference(1, vk::ImageLayout::eColorAttachmentOptimal) };
			const vk::AttachmentReference sceneDepthRef(2, vk::ImageLayout::eDepthStencilAttachmentOptimal);
			vk::SubpassDescription sceneSubpass{};
			sceneSubpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			sceneSubpass.colorAttachmentCount = static_cast<uint32_t>(sceneColorRefs.size());
			sceneSubpass.pColorAttachments = sceneColorRefs.data();
			sceneSubpass.pDepthStencilAttachment = &sceneDepthRef;

			vk::AttachmentDescription color{};
			color.format = HdrFormat;
			color.samples = vk::SampleCountFlagBits::e1;
			color.loadOp = vk::AttachmentLoadOp::eClear;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = vk::ImageLayout::eUndefined;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentDescription ambient = color;
			ambient.format = AmbientFormat;

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

			const std::array<vk::AttachmentDescription, 3> attachments = { color, ambient, depth };
			SceneRenderPass = vk::raii::RenderPass(context.GetDevice(), vk::RenderPassCreateInfo({}, attachments, sceneSubpass, dependencies));
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

	void VulkanViewportTarget::CreateMultisampledScenePass(VulkanContext& context)
	{
		// 0 to 2: the multisampled HDR colour, ambient light and depth, cleared, not kept; 3 to 5:
		// the single-sampled ones they are resolved into at the pass's end, kept for the passes
		// after. A depth resolve needs the second version of render passes (Vulkan 1.2).
		vk::AttachmentDescription2 color{};
		color.format = HdrFormat;
		color.samples = Samples;
		color.loadOp = vk::AttachmentLoadOp::eClear;
		color.storeOp = vk::AttachmentStoreOp::eDontCare;
		color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		color.initialLayout = vk::ImageLayout::eUndefined;
		color.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentDescription2 ambient = color;
		ambient.format = AmbientFormat;

		vk::AttachmentDescription2 depth = color;
		depth.format = DepthFormat;
		depth.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentDescription2 resolvedColor = color;
		resolvedColor.samples = vk::SampleCountFlagBits::e1;
		resolvedColor.loadOp = vk::AttachmentLoadOp::eDontCare;
		resolvedColor.storeOp = vk::AttachmentStoreOp::eStore;
		resolvedColor.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentDescription2 resolvedAmbient = resolvedColor;
		resolvedAmbient.format = AmbientFormat;

		vk::AttachmentDescription2 resolvedDepth = resolvedColor;
		resolvedDepth.format = DepthFormat;
		resolvedDepth.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		const std::array<vk::AttachmentReference2, 2> colorRefs = { vk::AttachmentReference2(0, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor),
			vk::AttachmentReference2(1, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor) };
		const std::array<vk::AttachmentReference2, 2> resolveRefs = { vk::AttachmentReference2(3, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor),
			vk::AttachmentReference2(4, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor) };
		const vk::AttachmentReference2 depthRef(2, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);
		const vk::AttachmentReference2 resolvedDepthRef(5, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);

		// The depth's nearest sample (ChooseDepthResolveMode()); a format with a stencil, which
		// nothing uses, resolves both by the first sample, which any device can do for both
		const bool bStencil = DepthFormat == vk::Format::eD32SfloatS8Uint;
		const vk::SubpassDescriptionDepthStencilResolve depthResolve(bStencil ? vk::ResolveModeFlagBits::eSampleZero : DepthResolveMode,
			bStencil ? vk::ResolveModeFlagBits::eSampleZero : vk::ResolveModeFlagBits::eNone, &resolvedDepthRef);
		vk::SubpassDescription2 subpass{};
		subpass.pNext = &depthResolve;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
		subpass.pColorAttachments = colorRefs.data();
		subpass.pResolveAttachments = resolveRefs.data();
		subpass.pDepthStencilAttachment = &depthRef;

		// As with one sample; the resolves write in the colour output stage, the depth's too
		std::array<vk::SubpassDependency2, 2> dependencies{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		// The passes after sample the resolved colours and depth, and test against the depth
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		const std::array<vk::AttachmentDescription2, 6> attachments = { color, ambient, depth, resolvedColor, resolvedAmbient, resolvedDepth };
		SceneRenderPass = vk::raii::RenderPass(context.GetDevice(), vk::RenderPassCreateInfo2({}, attachments, subpass, dependencies));
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

		// With more samples, the multisampled targets first, then those they resolve into
		const std::array<vk::ImageView, 3> sceneAttachments = { *HdrImageView, *AmbientImageView, *DepthImageView };
		const bool bMultisampled = Samples != vk::SampleCountFlagBits::e1;
		std::array<vk::ImageView, 6> multisampledAttachments{};
		if (bMultisampled)
		{
			multisampledAttachments = { *MultisampledHdrImageView, *MultisampledAmbientImageView, *MultisampledDepthImageView, *HdrImageView, *AmbientImageView, *DepthImageView };
		}
		fbInfo.renderPass = *SceneRenderPass;
		fbInfo.attachmentCount = bMultisampled ? static_cast<uint32_t>(multisampledAttachments.size()) : static_cast<uint32_t>(sceneAttachments.size());
		fbInfo.pAttachments = bMultisampled ? multisampledAttachments.data() : sceneAttachments.data();
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
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::SharingMode::eExclusive,
			{},
			vk::ImageLayout::eUndefined);

		DepthImage = vk::raii::Image(context.GetDevice(), imageInfo);
		DepthMemory = AllocateImageMemory(context, DepthImage);
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
		// Floating point only: reversed depth is fine far away because a float is finest near 0
		return FindSupportedFormat(
			physicalDevice,
			{ vk::Format::eD32Sfloat,
				vk::Format::eD32SfloatS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment);
	}

}
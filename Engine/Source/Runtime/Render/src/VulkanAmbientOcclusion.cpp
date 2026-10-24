#include "NyxPCH.h"
#include "VulkanAmbientOcclusion.h"

#include "AmbientOcclusion.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"
#include "VulkanViewportTarget.h"

#include <array>
#include <span>

namespace Nyx
{
	namespace
	{
		// One colour target, its pass after the pass before wrote what it reads (or this target),
		// and before the next reads it
		vk::raii::RenderPass MakeRenderPass(vk::raii::Device& device, vk::Format format, bool bLoad)
		{
			const vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);
			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;

			const std::array<vk::SubpassDependency, 2> dependencies = {
				vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
					vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead,
					vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead),
				vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
					vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead),
			};

			vk::AttachmentDescription color{};
			color.format = format;
			color.samples = vk::SampleCountFlagBits::e1;
			color.loadOp = bLoad ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eDontCare;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = bLoad ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			return vk::raii::RenderPass(device, vk::RenderPassCreateInfo({}, color, subpass, dependencies));
		}
	}

	VulkanAmbientOcclusion::VulkanAmbientOcclusion(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanAmbientOcclusion::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		CreateRenderPasses();
		CreatePipelines();
	}

	void VulkanAmbientOcclusion::Shutdown()
	{
		DepthPipeline = nullptr;
		OcclusionPipeline = nullptr;
		BlurPipeline = nullptr;
		CompositePipeline = nullptr;
		DepthPass = nullptr;
		OcclusionPass = nullptr;
		CompositePass = nullptr;
		Bindless = nullptr;
	}

	void VulkanAmbientOcclusion::CreateRenderPasses()
	{
		vk::raii::Device& device = Context.GetDevice();
		DepthPass = MakeRenderPass(device, DepthFormat, false);
		OcclusionPass = MakeRenderPass(device, OcclusionFormat, false);
		// The scene as its pass left it, added to
		CompositePass = MakeRenderPass(device, VulkanViewportTarget::HdrFormat, true);
	}

	void VulkanAmbientOcclusion::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		const std::string vertexName = "occlusionVertex";
		vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv("AmbientOcclusion.slang", vertexName));

		// One triangle over the target, no vertices, no depth
		vk::PipelineVertexInputStateCreateInfo vertexInput{};
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		const std::span<const vk::DynamicState> dynamicStates = std::span<const vk::DynamicState>(MeshDynamicStates).first(2);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

		const auto make = [&](const std::string& fragmentName, vk::RenderPass renderPass, bool bAdd)
		{
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv("AmbientOcclusion.slang", fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
			};
			// The occluded sky's light is added to the scene; its alpha kept as the scene left it
			vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			if (bAdd)
			{
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
				colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
				colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
				colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
				colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
				colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
			}
			vk::PipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			vk::GraphicsPipelineCreateInfo info{};
			info.stageCount = static_cast<uint32_t>(stages.size());
			info.pStages = stages.data();
			info.pVertexInputState = &vertexInput;
			info.pInputAssemblyState = &inputAssembly;
			info.pViewportState = &viewportState;
			info.pRasterizationState = &rasterizer;
			info.pMultisampleState = &multisampling;
			info.pDepthStencilState = &depthStencil;
			info.pColorBlendState = &colorBlending;
			info.pDynamicState = &dynamicState;
			info.layout = ScenePipelineLayout;
			info.renderPass = renderPass;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		DepthPipeline = make("occlusionDepthFragment", *DepthPass, false);
		OcclusionPipeline = make("occlusionFragment", *OcclusionPass, false);
		BlurPipeline = make("occlusionBlurFragment", *OcclusionPass, false);
		CompositePipeline = make("occlusionCompositeFragment", *CompositePass, true);
	}

	void VulkanAmbientOcclusion::CreateTarget(AmbientOcclusionTarget& target, vk::Format format, vk::RenderPass renderPass, vk::Extent2D extent)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = format;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		target.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = target.Image.getMemoryRequirements();
		target.Memory = vk::raii::DeviceMemory(device,
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		target.Image.bindMemory(*target.Memory, 0);
		const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		target.View = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *target.Image, vk::ImageViewType::e2D, format, {}, whole));
		const vk::ImageView attachment = *target.View;
		target.Framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, renderPass, attachment, extent.width, extent.height, 1));

		// Readable before its first draw, as a slot in the bindless array must be
		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *target.Image, whole, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));
		target.Slot = Bindless->AddTexture(attachment);
	}

	void VulkanAmbientOcclusion::ReleaseTarget(AmbientOcclusionTarget& target)
	{
		Bindless->RemoveTexture(target.Slot);
		target.Slot = 0;
		target.Framebuffer = nullptr;
		target.View = nullptr;
		target.Image = nullptr;
		target.Memory = nullptr;
	}

	void VulkanAmbientOcclusion::Prepare(SceneViewAmbientOcclusion& occlusion, const VulkanViewportTarget& target, const SceneViewAmbientOcclusionSettings& settings, bool bTargetsRemade)
	{
		occlusion.Settings = settings;
		if (!settings.bAmbientOcclusion)
		{
			Release(occlusion);
			return;
		}

		const vk::Extent2D viewExtent = target.GetExtent();
		const glm::uvec2 size = GetAmbientOcclusionSize(viewExtent.width, viewExtent.height);
		const vk::Extent2D extent(size.x, size.y);
		if (occlusion.Extent == extent && !bTargetsRemade)
		{
			return;
		}
		Release(occlusion);

		CreateTarget(occlusion.Depth, DepthFormat, *DepthPass, extent);
		CreateTarget(occlusion.Occlusion, OcclusionFormat, *OcclusionPass, extent);
		CreateTarget(occlusion.Blurred, OcclusionFormat, *OcclusionPass, extent);
		const vk::ImageView hdr = target.GetHdrImageView();
		occlusion.Composite = vk::raii::Framebuffer(Context.GetDevice(), vk::FramebufferCreateInfo({}, *CompositePass, hdr, viewExtent.width, viewExtent.height, 1));
		occlusion.Extent = extent;
	}

	void VulkanAmbientOcclusion::Release(SceneViewAmbientOcclusion& occlusion)
	{
		if (occlusion.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		ReleaseTarget(occlusion.Depth);
		ReleaseTarget(occlusion.Occlusion);
		ReleaseTarget(occlusion.Blurred);
		occlusion.Composite = nullptr;
		occlusion.Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanAmbientOcclusion::Draw(SceneViewInstance& view, uint32_t sceneDepthSlot, vk::raii::CommandBuffer& cmd)
	{
		SceneViewAmbientOcclusion& occlusion = view.AmbientOcclusion;
		if (!occlusion.Settings.bAmbientOcclusion || occlusion.Extent.width == 0)
		{
			return;
		}

		// The scene's depth, read rather than tested against, until the picture pass tests against it
		const vk::Image depthImage = view.RenderTarget.GetDepthImage();
		const vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		const vk::Extent2D viewExtent = view.RenderTarget.GetExtent();
		const vk::Extent2D extent = occlusion.Extent;
		DrawPass(view, cmd, *DepthPass, *occlusion.Depth.Framebuffer, extent, *DepthPipeline, sceneDepthSlot, 0, viewExtent, glm::vec2(0.0f));
		DrawPass(view, cmd, *OcclusionPass, *occlusion.Occlusion.Framebuffer, extent, *OcclusionPipeline, occlusion.Depth.Slot, 0, extent, glm::vec2(0.0f));
		DrawPass(view, cmd, *OcclusionPass, *occlusion.Blurred.Framebuffer, extent, *BlurPipeline, occlusion.Occlusion.Slot, occlusion.Depth.Slot, extent, glm::vec2(1.0f, 0.0f));
		DrawPass(view, cmd, *OcclusionPass, *occlusion.Occlusion.Framebuffer, extent, *BlurPipeline, occlusion.Blurred.Slot, occlusion.Depth.Slot, extent, glm::vec2(0.0f, 1.0f));
		// The occluded sky's light into the scene (the composite reads its sources from the view's set)
		DrawPass(view, cmd, *CompositePass, *occlusion.Composite, viewExtent, *CompositePipeline, 0, 0, extent, glm::vec2(0.0f));

		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
	}

	void VulkanAmbientOcclusion::DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, vk::RenderPass renderPass, vk::Framebuffer framebuffer, vk::Extent2D extent,
		vk::Pipeline pipeline, uint32_t firstSlot, uint32_t secondSlot, vk::Extent2D sourceExtent, const glm::vec2& values)
	{
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = renderPass;
		begin.framebuffer = framebuffer;
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		BindSceneSets(view, cmd);

		// AmbientOcclusion.slang's sources and values, in the draw's push constants
		ObjectConstants object{};
		object.MaterialOffset = firstSlot;
		object.EntityId = secondSlot;
		object.CustomData = glm::vec4(1.0f / glm::vec2(sourceExtent.width, sourceExtent.height), 1.0f / glm::vec2(extent.width, extent.height));
		object.CustomData2 = values;
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
}

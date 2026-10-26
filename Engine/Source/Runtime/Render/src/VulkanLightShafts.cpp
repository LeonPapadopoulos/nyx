#include "NyxPCH.h"
#include "VulkanLightShafts.h"

#include "LightShafts.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"

#include <array>
#include <span>

namespace Nyx
{
	VulkanLightShafts::VulkanLightShafts(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanLightShafts::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		CreateRenderPass();
		CreatePipelines();
	}

	void VulkanLightShafts::Shutdown()
	{
		PrefilterPipeline = nullptr;
		BlurPipeline = nullptr;
		SmoothPipeline = nullptr;
		RenderPass = nullptr;
		Bindless = nullptr;
	}

	void VulkanLightShafts::CreateRenderPass()
	{
		// One colour target written whole, after the pass before wrote what it reads, and before the
		// next (or the tonemap) reads it
		const vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead),
			vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead),
		};

		vk::AttachmentDescription color{};
		color.format = Format;
		color.samples = vk::SampleCountFlagBits::e1;
		color.loadOp = vk::AttachmentLoadOp::eDontCare;
		color.storeOp = vk::AttachmentStoreOp::eStore;
		color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		color.initialLayout = vk::ImageLayout::eUndefined;
		color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		RenderPass = vk::raii::RenderPass(Context.GetDevice(), vk::RenderPassCreateInfo({}, color, subpass, dependencies));
	}

	void VulkanLightShafts::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		const std::string vertexName = "lightShaftsVertex";
		vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv("LightShafts.slang", vertexName));

		// One triangle over the target, no vertices, no depth, no blending
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
		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR;
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		const std::span<const vk::DynamicState> dynamicStates = std::span<const vk::DynamicState>(MeshDynamicStates).first(2);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

		const auto make = [&](const std::string& fragmentName)
		{
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv("LightShafts.slang", fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
			};
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
			info.renderPass = *RenderPass;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		PrefilterPipeline = make("lightShaftsPrefilterFragment");
		BlurPipeline = make("lightShaftsBlurFragment");
		SmoothPipeline = make("lightShaftsSmoothFragment");
	}

	void VulkanLightShafts::CreateTarget(LightShaftsTarget& target, vk::Extent2D extent)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = Format;
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
		target.View = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *target.Image, vk::ImageViewType::e2D, Format, {}, whole));
		const vk::ImageView attachment = *target.View;
		target.Framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, *RenderPass, attachment, extent.width, extent.height, 1));

		// Readable before its first draw, as a slot in the bindless array must be
		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *target.Image, whole, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));
		target.Slot = Bindless->AddTexture(attachment);
	}

	void VulkanLightShafts::ReleaseTarget(LightShaftsTarget& target)
	{
		Bindless->RemoveTexture(target.Slot);
		target.Slot = 0;
		target.Framebuffer = nullptr;
		target.View = nullptr;
		target.Image = nullptr;
		target.Memory = nullptr;
	}

	void VulkanLightShafts::Prepare(SceneViewLightShafts& shafts, vk::Extent2D viewExtent, const SceneViewLightShaftsSettings& settings)
	{
		shafts.Settings = settings;
		if (!settings.bLightShafts)
		{
			Release(shafts);
			return;
		}

		const glm::uvec2 size = GetLightShaftsSize(viewExtent.width, viewExtent.height);
		const vk::Extent2D extent(size.x, size.y);
		if (shafts.Extent == extent)
		{
			return;
		}
		Release(shafts);

		CreateTarget(shafts.Gathered, extent);
		CreateTarget(shafts.Blurred, extent);
		shafts.Extent = extent;
	}

	void VulkanLightShafts::Release(SceneViewLightShafts& shafts)
	{
		if (shafts.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		ReleaseTarget(shafts.Gathered);
		ReleaseTarget(shafts.Blurred);
		shafts.Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanLightShafts::Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		SceneViewLightShafts& shafts = view.LightShafts;
		if (!shafts.Settings.IsDrawn() || shafts.Extent.width == 0)
		{
			return;
		}

		// The scene's depth, read rather than tested against, until the picture pass tests against it
		const vk::Image depthImage = view.RenderTarget.GetDepthImage();
		const vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		// The light that may stream, from the scene and its depth (the view's set); gathered along
		// the lines to the sun; evened out along them, back into the first, which the tonemap reads
		const vk::Extent2D extent = shafts.Extent;
		DrawPass(view, cmd, shafts.Gathered, extent, *PrefilterPipeline, 0);
		DrawPass(view, cmd, shafts.Blurred, extent, *BlurPipeline, shafts.Gathered.Slot);
		DrawPass(view, cmd, shafts.Gathered, extent, *SmoothPipeline, shafts.Blurred.Slot);

		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
	}

	void VulkanLightShafts::DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, const LightShaftsTarget& target, vk::Extent2D extent, vk::Pipeline pipeline, uint32_t sourceSlot)
	{
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = *RenderPass;
		begin.framebuffer = *target.Framebuffer;
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		BindSceneSets(view, cmd);

		// LightShafts.slang's source and the target's texel, in the draw's push constants
		ObjectConstants object{};
		object.MaterialOffset = sourceSlot;
		object.CustomData = glm::vec4(1.0f / glm::vec2(extent.width, extent.height), glm::vec2(extent.width, extent.height));
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
}

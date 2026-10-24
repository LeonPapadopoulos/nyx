#include "NyxPCH.h"
#include "VulkanBloom.h"

#include "Bloom.h"
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
	namespace
	{
		// Drawn into and blended, filtered when read: B10G11R11 holds light above 1 at 4 bytes a
		// texel, enough for a blur
		bool CanBloomInto(vk::PhysicalDevice device, vk::Format format)
		{
			const vk::FormatFeatureFlags needed = vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend
				| vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
			return (device.getFormatProperties(format).optimalTilingFeatures & needed) == needed;
		}
	}

	VulkanBloom::VulkanBloom(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanBloom::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		Format = CanBloomInto(*Context.GetPhysicalDevice(), vk::Format::eB10G11R11UfloatPack32) ? vk::Format::eB10G11R11UfloatPack32 : vk::Format::eR16G16B16A16Sfloat;
		CreateRenderPasses();
		CreatePipelines();
	}

	void VulkanBloom::Shutdown()
	{
		PrefilterPipeline = nullptr;
		DownsamplePipeline = nullptr;
		UpsamplePipeline = nullptr;
		DownPass = nullptr;
		UpPass = nullptr;
		Bindless = nullptr;
	}

	void VulkanBloom::CreateRenderPasses()
	{
		const vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		// After the pass before wrote what this one reads (or this level), and before the next
		// reads this level: the tonemap, or the next pass of the chain
		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead),
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
		DownPass = vk::raii::RenderPass(Context.GetDevice(), vk::RenderPassCreateInfo({}, color, subpass, dependencies));

		// The level as the way down left it, blended into
		color.loadOp = vk::AttachmentLoadOp::eLoad;
		color.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		UpPass = vk::raii::RenderPass(Context.GetDevice(), vk::RenderPassCreateInfo({}, color, subpass, dependencies));
	}

	void VulkanBloom::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		const std::string vertexName = "bloomVertex";
		vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv("Bloom.slang", vertexName));

		// One triangle over the level, no vertices, no depth
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

		const auto make = [&](const std::string& fragmentName, bool bBlend)
		{
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv("Bloom.slang", fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
			};
			// The way up mixes the level below into this one by the radius, the shader's alpha:
			// this level times 1 - radius, plus the one below times the radius
			vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			if (bBlend)
			{
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
				colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
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
			info.renderPass = *DownPass;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		PrefilterPipeline = make("bloomPrefilterFragment", false);
		DownsamplePipeline = make("bloomDownsampleFragment", false);
		UpsamplePipeline = make("bloomUpsampleFragment", true);
	}

	void VulkanBloom::Prepare(SceneViewBloom& bloom, vk::Extent2D viewExtent, const SceneViewBloomSettings& settings)
	{
		bloom.Settings = settings;
		if (!settings.bBloom)
		{
			Release(bloom);
			return;
		}

		const uint32_t levels = GetBloomLevelCount(viewExtent.width, viewExtent.height, settings.Levels);
		const glm::uvec2 first = GetBloomLevelSize(viewExtent.width, viewExtent.height, 0);
		const vk::Extent2D extent(first.x, first.y);
		if (bloom.Levels == levels && bloom.Extent == extent)
		{
			return;
		}
		Release(bloom);

		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = Format;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = levels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		bloom.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = bloom.Image.getMemoryRequirements();
		bloom.Memory = vk::raii::DeviceMemory(device,
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		bloom.Image.bindMemory(*bloom.Memory, 0);

		for (uint32_t level = 0; level < levels; ++level)
		{
			const glm::uvec2 size = GetBloomLevelSize(viewExtent.width, viewExtent.height, level);
			const vk::ImageSubresourceRange one(vk::ImageAspectFlagBits::eColor, level, 1, 0, 1);
			bloom.Views.emplace_back(device, vk::ImageViewCreateInfo({}, *bloom.Image, vk::ImageViewType::e2D, Format, {}, one));
			const vk::ImageView attachment = *bloom.Views.back();
			bloom.Framebuffers.emplace_back(device, vk::FramebufferCreateInfo({}, *DownPass, attachment, size.x, size.y, 1));
			bloom.Slots.push_back(Bindless->AddTexture(attachment));
		}

		// Readable before its first draw, as a slot in the bindless array must be
		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *bloom.Image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, 1), vk::ImageLayout::eUndefined,
			vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {}, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));

		bloom.Extent = extent;
		bloom.Levels = levels;
	}

	void VulkanBloom::Release(SceneViewBloom& bloom)
	{
		if (bloom.Levels == 0)
		{
			return;
		}
		Context.WaitIdle();
		for (const uint32_t slot : bloom.Slots)
		{
			Bindless->RemoveTexture(slot);
		}
		bloom.Slots.clear();
		bloom.Framebuffers.clear();
		bloom.Views.clear();
		bloom.Image = nullptr;
		bloom.Memory = nullptr;
		bloom.Extent = vk::Extent2D{ 0, 0 };
		bloom.Levels = 0;
	}

	void VulkanBloom::Draw(SceneViewInstance& view, uint32_t hdrSlot, vk::raii::CommandBuffer& cmd)
	{
		SceneViewBloom& bloom = view.Bloom;
		if (!bloom.Settings.bBloom || bloom.Levels == 0)
		{
			return;
		}

		// Down: the scene's bright parts into the first level, then each level halved into the next
		const vk::Extent2D viewExtent = view.RenderTarget.GetExtent();
		const auto levelSize = [&](uint32_t level)
		{
			return GetBloomLevelSize(viewExtent.width, viewExtent.height, level);
		};
		DrawPass(view, cmd, *DownPass, 0, *PrefilterPipeline, hdrSlot, glm::uvec2(viewExtent.width, viewExtent.height), glm::vec2(bloom.Settings.Threshold, bloom.Settings.Knee));
		for (uint32_t level = 1; level < bloom.Levels; ++level)
		{
			DrawPass(view, cmd, *DownPass, level, *DownsamplePipeline, bloom.Slots[level - 1], levelSize(level - 1), glm::vec2(0.0f));
		}
		// Up: each level mixed into the one above, by the radius
		for (uint32_t level = bloom.Levels - 1; level > 0; --level)
		{
			DrawPass(view, cmd, *UpPass, level - 1, *UpsamplePipeline, bloom.Slots[level], levelSize(level), glm::vec2(bloom.Settings.Radius, 0.0f));
		}
	}

	void VulkanBloom::DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, vk::RenderPass renderPass, uint32_t level, vk::Pipeline pipeline, uint32_t sourceSlot,
		const glm::uvec2& sourceSize, const glm::vec2& values)
	{
		SceneViewBloom& bloom = view.Bloom;
		const glm::uvec2 size = GetBloomLevelSize(view.RenderTarget.GetExtent().width, view.RenderTarget.GetExtent().height, level);
		const vk::Extent2D extent(size.x, size.y);
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = renderPass;
		begin.framebuffer = *bloom.Framebuffers[level];
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		BindSceneSets(view, cmd);

		// Bloom.slang's BloomConstants, in the draw's push constants
		ObjectConstants object{};
		object.MaterialOffset = sourceSlot;
		object.CustomData = glm::vec4(1.0f / glm::vec2(sourceSize), 1.0f / glm::vec2(size));
		object.CustomData2 = values;
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
}

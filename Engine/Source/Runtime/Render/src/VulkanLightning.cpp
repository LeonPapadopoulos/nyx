#include "NyxPCH.h"
#include "VulkanLightning.h"

#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"
#include "VulkanViewportTarget.h"

#include <cstring>

namespace Nyx
{
	namespace
	{
		constexpr const char* Program = "Lightning.slang";

		// A pass of one colour target in `format`: loaded (the HDR scene, drawn over) or cleared (the
		// view's lightning picture), read by fragment shaders before and after
		vk::raii::RenderPass MakePass(VulkanContext& context, vk::Format format, bool bClear)
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
			color.loadOp = bClear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = bClear ? vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			return vk::raii::RenderPass(context.GetDevice(), vk::RenderPassCreateInfo({}, color, subpass, dependencies));
		}
	}

	VulkanLightning::VulkanLightning(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanLightning::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		StripsPass = MakePass(Context, VulkanViewportTarget::HdrFormat, true);
		Pass = MakePass(Context, VulkanViewportTarget::HdrFormat, false);
		CreatePipelines();
	}

	void VulkanLightning::Shutdown()
	{
		Pipeline = nullptr;
		CompositePipeline = nullptr;
		StripsPass = nullptr;
		Pass = nullptr;
		Stream = nullptr;
		StreamMemory = nullptr;
		StreamCapacity = 0;
		RetiredStreams.clear();
	}

	void VulkanLightning::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
		// No depth buffer: each pixel is tested against the scene's depth in the shader
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

		const auto make = [&](const char* vertexName, const char* fragmentName, const vk::PipelineVertexInputStateCreateInfo& vertexInput, vk::BlendOp blendOp,
							  vk::RenderPass renderPass)
		{
			vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv(Program, vertexName));
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv(Program, fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName),
			};
			vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.colorBlendOp = blendOp;
			colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
			colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
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

		// The strips' vertices (LightningVertex): where, and the stretch each quad draws; each pixel
		// the brightest of the strips over it
		const vk::VertexInputBindingDescription binding(0, sizeof(LightningVertex), vk::VertexInputRate::eVertex);
		const std::array<vk::VertexInputAttributeDescription, 4> attributes = {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(LightningVertex, Position)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(LightningVertex, Start)),
			vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(LightningVertex, End)),
			vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(LightningVertex, StartIntensity)),
		};
		const vk::PipelineVertexInputStateCreateInfo stripsInput({}, binding, attributes);
		Pipeline = make("lightningVertex", "lightningFragment", stripsInput, vk::BlendOp::eMax, *StripsPass);

		// The picture over the scene: one triangle, added
		const vk::PipelineVertexInputStateCreateInfo noInput{};
		CompositePipeline = make("lightningCompositeVertex", "lightningCompositeFragment", noInput, vk::BlendOp::eAdd, *Pass);
	}

	void VulkanLightning::Prepare(SceneViewLightning& lightning, const VulkanViewportTarget& target, bool bTargetsRemade)
	{
		const vk::Extent2D extent = target.GetExtent();
		if (lightning.Extent == extent && !bTargetsRemade && *lightning.Framebuffer)
		{
			return;
		}
		// Nothing made until it first draws a bolt
		if (lightning.Draws.empty() && !*lightning.Framebuffer)
		{
			return;
		}
		ReleasePicture(lightning);

		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = VulkanViewportTarget::HdrFormat;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		lightning.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = lightning.Image.getMemoryRequirements();
		lightning.Memory = vk::raii::DeviceMemory(device,
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		lightning.Image.bindMemory(*lightning.Memory, 0);
		const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		lightning.View = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *lightning.Image, vk::ImageViewType::e2D, VulkanViewportTarget::HdrFormat, {}, whole));
		// Readable before it is first written, as a slot in the bindless array must be
		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *lightning.Image, whole, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));
		lightning.Slot = Bindless->AddTexture(*lightning.View);

		lightning.Strips = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, *StripsPass, *lightning.View, extent.width, extent.height, 1));
		const vk::ImageView hdr = target.GetHdrImageView();
		lightning.Framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, *Pass, hdr, extent.width, extent.height, 1));
		lightning.Extent = extent;
	}

	void VulkanLightning::ReleasePicture(SceneViewLightning& lightning)
	{
		if (lightning.Slot != 0)
		{
			Bindless->RemoveTexture(lightning.Slot);
		}
		lightning.Slot = 0;
		lightning.Strips = nullptr;
		lightning.Framebuffer = nullptr;
		lightning.View = nullptr;
		lightning.Image = nullptr;
		lightning.Memory = nullptr;
		lightning.Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanLightning::Release(SceneViewLightning& lightning)
	{
		ReleasePicture(lightning);
		lightning.Draws.clear();
	}

	void VulkanLightning::Upload(std::span<const LightningVertex> vertices)
	{
		if (vertices.empty())
		{
			return;
		}
		if (vertices.size() > StreamCapacity)
		{
			if (*Stream)
			{
				RetiredStreams.emplace_back(std::move(Stream), std::move(StreamMemory));
			}
			StreamCapacity = std::max<size_t>(vertices.size() + vertices.size() / 2, 4096);
			CreateBuffer(Context, StreamCapacity * sizeof(LightningVertex), vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, Stream, StreamMemory);
		}
		const vk::DeviceSize size = vertices.size_bytes();
		void* mapped = StreamMemory.mapMemory(0, size);
		std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
		StreamMemory.unmapMemory();
	}

	void VulkanLightning::ReleaseRetired()
	{
		RetiredStreams.clear();
	}

	void VulkanLightning::Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		SceneViewLightning& lightning = view.Lightning;
		if (lightning.Draws.empty() || !*lightning.Framebuffer || !*Stream)
		{
			return;
		}

		// The scene's depth, read rather than tested against, until the picture pass tests against it
		const vk::Image depthImage = view.RenderTarget.GetDepthImage();
		const vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		const vk::Extent2D extent = lightning.Extent;
		const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
		const vk::Rect2D area({ 0, 0 }, extent);

		// 1. The strips into the view's picture, each pixel the brightest over it
		const vk::ClearValue black(vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }));
		cmd.beginRenderPass(vk::RenderPassBeginInfo(*StripsPass, *lightning.Strips, area, black), vk::SubpassContents::eInline);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, area);
		BindSceneSets(view, cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *Pipeline);
		cmd.bindVertexBuffers(0, { *Stream }, { vk::DeviceSize{ 0 } });
		for (const SceneViewLightningDraw& draw : lightning.Draws)
		{
			// The look in the model matrix's columns (Lightning.slang)
			ObjectConstants object{};
			object.Model = glm::mat4(glm::vec4(draw.Look.Core, draw.Look.CoreFraction), glm::vec4(draw.Look.Body, draw.Look.BodyFraction),
				glm::vec4(draw.Look.Glow, draw.Look.SoftDistance), glm::vec4(0.0f, draw.Look.Sharpness, 0.0f, 1.0f));
			cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
			cmd.draw(draw.VertexCount, 1, draw.FirstVertex, 0);
		}
		cmd.endRenderPass();

		// 2. The picture added to the HDR scene
		cmd.beginRenderPass(vk::RenderPassBeginInfo(*Pass, *lightning.Framebuffer, area), vk::SubpassContents::eInline);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, area);
		BindSceneSets(view, cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *CompositePipeline);
		ObjectConstants composite{};
		composite.MaterialOffset = lightning.Slot;
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { composite });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();

		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
	}
}

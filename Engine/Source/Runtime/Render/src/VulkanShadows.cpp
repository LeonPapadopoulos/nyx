#include "NyxPCH.h"
#include "VulkanShadows.h"

#include "CameraProjection.h"
#include "Mesh.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>

namespace Nyx
{
	uint32_t VulkanShadows::ClampResolution(int32_t resolution)
	{
		const uint32_t wanted = static_cast<uint32_t>(std::clamp<int32_t>(resolution, MinResolution, MaxResolution));
		// The power of two nearest: the lower one, unless the one above is nearer
		const uint32_t lower = std::bit_floor(wanted);
		return wanted - lower > lower * 2 - wanted ? std::min(lower * 2, MaxResolution) : lower;
	}

	VulkanShadows::VulkanShadows(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanShadows::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		CreateRenderPass();
		CreateMap(InitialSize);
	}

	void VulkanShadows::Shutdown()
	{
		DestroyMap();
		CasterPipeline = nullptr;
		DebugPipeline = nullptr;
		RenderPass = nullptr;
		Bindless = nullptr;
	}

	void VulkanShadows::CreateRenderPass()
	{
		vk::AttachmentDescription depth{};
		depth.format = DepthFormat;
		depth.samples = vk::SampleCountFlagBits::e1;
		depth.loadOp = vk::AttachmentLoadOp::eClear;
		depth.storeOp = vk::AttachmentStoreOp::eStore;
		depth.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		depth.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		// Nothing of the last view's cascade is kept; left for the scene pass to sample
		depth.initialLayout = vk::ImageLayout::eUndefined;
		depth.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		const vk::AttachmentReference depthReference(0, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.pDepthStencilAttachment = &depthReference;

		// After the last view's scene pass read the layer, and before this view's reads it
		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite),
			vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eShaderRead),
		};

		vk::RenderPassCreateInfo info{};
		info.attachmentCount = 1;
		info.pAttachments = &depth;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = static_cast<uint32_t>(dependencies.size());
		info.pDependencies = dependencies.data();
		RenderPass = vk::raii::RenderPass(Context.GetDevice(), info);
	}

	void VulkanShadows::CreateMap(uint32_t size)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = DepthFormat;
		imageInfo.extent = vk::Extent3D(size, size, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = MaxShadowCascades;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		Image = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = Image.getMemoryRequirements();
		Memory = vk::raii::DeviceMemory(device,
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		Image.bindMemory(*Memory, 0);

		const vk::ImageSubresourceRange all(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, MaxShadowCascades);
		ArrayView = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *Image, vk::ImageViewType::e2DArray, DepthFormat, {}, all));
		for (uint32_t layer = 0; layer < MaxShadowCascades; ++layer)
		{
			const vk::ImageSubresourceRange one(vk::ImageAspectFlagBits::eDepth, 0, 1, layer, 1);
			LayerViews[layer] = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *Image, vk::ImageViewType::e2D, DepthFormat, {}, one));
			const vk::ImageView attachment = *LayerViews[layer];
			Framebuffers[layer] = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, *RenderPass, attachment, size, size, 1));
		}

		// Readable before any cascade is drawn into it: a view without shadows never samples it,
		// but its slot is a valid image
		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *Image, all, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));

		Size = size;
		Slot = Bindless->AddTexture(*ArrayView);
	}

	void VulkanShadows::DestroyMap()
	{
		if (Size == 0)
		{
			return;
		}
		Context.WaitIdle();
		if (Bindless)
		{
			Bindless->RemoveTexture(Slot);
		}
		Framebuffers = { nullptr, nullptr, nullptr, nullptr };
		LayerViews = { nullptr, nullptr, nullptr, nullptr };
		ArrayView = nullptr;
		Image = nullptr;
		Memory = nullptr;
		Size = 0;
		Slot = 0;
	}

	void VulkanShadows::EnsureSize(uint32_t size)
	{
		if (size == 0 || size == Size)
		{
			return;
		}
		DestroyMap();
		CreateMap(size);
	}

	void VulkanShadows::CreatePipelines(vk::RenderPass picturePass)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// The casters: depth only, the faces each material draws, as the light sees them
		{
			const std::string entryPoint = "shadowVertex";
			vk::raii::ShaderModule module = CreateShaderModule(Context, Programs.GetSpirv("Shadow.slang", entryPoint));
			const vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eVertex, *module, entryPoint.c_str());

			// The vertices' positions, and each instance's transform from the second stream
			const std::array<vk::VertexInputBindingDescription, 2> bindings = { GetVertexBindingDescription(), GetInstanceBindingDescription() };
			std::vector<vk::VertexInputAttributeDescription> attributes = { GetVertexAttributeDescriptions()[0] };
			for (const vk::VertexInputAttributeDescription& attribute : GetInstanceAttributeDescriptions())
			{
				attributes.push_back(attribute);
			}
			vk::PipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
			vertexInput.pVertexBindingDescriptions = bindings.data();
			vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
			vertexInput.pVertexAttributeDescriptions = attributes.data();

			vk::PipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.polygonMode = vk::PolygonMode::eFill;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = vk::CullModeFlagBits::eBack;
			rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

			vk::PipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = DepthTestNearer;

			vk::PipelineColorBlendStateCreateInfo colorBlending{};
			vk::PipelineDynamicStateCreateInfo dynamicState({}, MeshDynamicStates);

			vk::GraphicsPipelineCreateInfo info{};
			info.stageCount = 1;
			info.pStages = &stage;
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
			CasterPipeline = vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		}

		// The debug view: squares over the picture, no vertices, no depth
		{
			const std::string vertexName = "shadowDebugVertex";
			const std::string fragmentName = "shadowDebugFragment";
			vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv("Shadow.slang", vertexName));
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv("Shadow.slang", fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
			};
			vk::PipelineVertexInputStateCreateInfo vertexInput{};
			vk::PipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.polygonMode = vk::PolygonMode::eFill;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = vk::CullModeFlagBits::eNone;
			vk::PipelineDepthStencilStateCreateInfo depthStencil{};
			vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			vk::PipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;
			const std::span<const vk::DynamicState> dynamicStates = std::span<const vk::DynamicState>(MeshDynamicStates).first(2);
			vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

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
			info.renderPass = picturePass;
			DebugPipeline = vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		}
	}

	void VulkanShadows::BeginCascade(vk::raii::CommandBuffer& cmd, uint32_t cascade, uint32_t resolution)
	{
		resolution = std::min(resolution, Size);
		const vk::ClearValue clear = vk::ClearDepthStencilValue(FarPlaneDepth, 0);
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = *RenderPass;
		begin.framebuffer = *Framebuffers[cascade];
		begin.renderArea = vk::Rect2D({ 0, 0 }, { resolution, resolution });
		begin.clearValueCount = 1;
		begin.pClearValues = &clear;
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(resolution), static_cast<float>(resolution), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, { resolution, resolution }));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *CasterPipeline);
	}

	void VulkanShadows::EndCascade(vk::raii::CommandBuffer& cmd)
	{
		cmd.endRenderPass();
	}

	void VulkanShadows::DrawDebug(SceneViewInstance& view, uint32_t cascadeCount, vk::raii::CommandBuffer& cmd)
	{
		if (cascadeCount == 0)
		{
			return;
		}
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *DebugPipeline);
		BindSceneSets(view, cmd);

		// Squares a fifth of the picture's height, along its bottom; clip space's y is down
		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		const float height = 0.4f;
		const float width = height * static_cast<float>(extent.height) / static_cast<float>(std::max(extent.width, 1u));
		const float margin = 0.03f;
		for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
		{
			const float left = -1.0f + margin + static_cast<float>(cascade) * (width + margin * 0.5f);
			ObjectConstants object{};
			object.CustomData = glm::vec4(left, 1.0f - margin - height, left + width, 1.0f - margin);
			object.CustomData2 = glm::vec2(static_cast<float>(cascade), 0.0f);
			cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
			cmd.draw(6, 1, 0, 0);
		}
	}
}

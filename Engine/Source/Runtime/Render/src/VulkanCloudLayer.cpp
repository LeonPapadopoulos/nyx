#include "NyxPCH.h"
#include "VulkanCloudLayer.h"

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
		constexpr const char* NoiseProgram = "CloudNoise.slang";
		constexpr const char* CloudProgram = "CloudLayer.slang";

		// NoiseDispatch in CloudNoise.slang
		struct NoiseDispatch
		{
			uint32_t Size = 0;
			uint32_t Seed = 0;
			uint32_t Padding0 = 0;
			uint32_t Padding1 = 0;
		};
		static_assert(sizeof(NoiseDispatch) == 16, "NoiseDispatch must match CloudNoise.slang");

		uint32_t Groups(uint32_t size)
		{
			return (size + 7) / 8;
		}

		vk::raii::DeviceMemory AllocateFor(VulkanContext& context, const vk::raii::Image& image)
		{
			const vk::MemoryRequirements requirements = image.getMemoryRequirements();
			return vk::raii::DeviceMemory(context.GetDevice(),
				vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		}

		// Readable before it is first written, as a slot in the bindless array must be
		void MakeReadable(VulkanContext& context, vk::Image image, const vk::ImageSubresourceRange& range)
		{
			vk::raii::CommandBuffer commands = BeginSingleTimeCommands(context);
			TransitionImageLayout(*commands, image, range, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
				vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
			EndSingleTimeCommands(context, std::move(commands));
		}
	}

	VulkanCloudLayer::VulkanCloudLayer(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanCloudLayer::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);

		vk::raii::Device& device = Context.GetDevice();
		const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
			vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
		};
		NoiseSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
		const vk::DescriptorSetLayout setLayout = *NoiseSetLayout;
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(NoiseDispatch));
		NoisePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, setLayout, pushRange));

		CreateRenderPass();
		CreatePipelines();
	}

	void VulkanCloudLayer::Shutdown()
	{
		NoisePipeline = nullptr;
		DownsamplePipeline = nullptr;
		CloudPipeline = nullptr;
		RenderPass = nullptr;
		NoisePipelineLayout = nullptr;
		NoiseSetLayout = nullptr;
		Bindless = nullptr;
	}

	void VulkanCloudLayer::CreateRenderPass()
	{
		// One colour target written whole, after the frame before's scene pass and light shafts read
		// it, before this one's read it
		const vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
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

	void VulkanCloudLayer::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		const auto makeCompute = [&](const char* entryPoint)
		{
			vk::raii::ShaderModule module = CreateShaderModule(Context, Programs.GetSpirv(NoiseProgram, entryPoint));
			vk::ComputePipelineCreateInfo info{};
			info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint);
			info.layout = *NoisePipelineLayout;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		NoisePipeline = makeCompute("cloudNoise");
		DownsamplePipeline = makeCompute("cloudNoiseDownsample");

		// One triangle over the target, no vertices, no depth, no blending
		const std::string vertexName = "cloudLayerVertex";
		const std::string fragmentName = "cloudLayerFragment";
		vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv(CloudProgram, vertexName));
		vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv(CloudProgram, fragmentName));
		const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
		};
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
		info.renderPass = *RenderPass;
		CloudPipeline = vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
	}

	void VulkanCloudLayer::Prepare(SceneViewCloudLayer& layer, vk::Extent2D viewExtent, const SceneViewCloudLayerSettings& settings)
	{
		layer.Settings = settings;
		if (!settings.bClouds)
		{
			Release(layer);
			return;
		}
		if (layer.NoiseSlot == 0)
		{
			MakeNoise(layer);
		}
		if (layer.NoiseSeed != settings.Seed)
		{
			layer.NoiseSeed = settings.Seed;
			layer.bNoisePending = true;
		}
		if (layer.Extent != viewExtent)
		{
			ReleaseTarget(layer);
			MakeTarget(layer, viewExtent);
		}
	}

	void VulkanCloudLayer::Release(SceneViewCloudLayer& layer)
	{
		if (layer.NoiseSlot == 0 && layer.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		ReleaseNoise(layer);
		ReleaseTarget(layer);
	}

	void VulkanCloudLayer::Invalidate(SceneViewCloudLayer& layer)
	{
		layer.bNoisePending = layer.NoiseSlot != 0;
	}

	void VulkanCloudLayer::MakeNoise(SceneViewCloudLayer& layer)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = Format;
		imageInfo.extent = vk::Extent3D(CloudNoiseSize, CloudNoiseSize, 1);
		imageInfo.mipLevels = CloudNoiseLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		layer.Noise = vk::raii::Image(device, imageInfo);
		layer.NoiseMemory = AllocateFor(Context, layer.Noise);
		layer.Noise.bindMemory(*layer.NoiseMemory, 0);

		const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, CloudNoiseLevels, 0, 1);
		layer.NoiseView = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *layer.Noise, vk::ImageViewType::e2D, Format, {}, whole));
		for (uint32_t level = 0; level < CloudNoiseLevels; ++level)
		{
			const vk::ImageSubresourceRange mip(vk::ImageAspectFlagBits::eColor, level, 1, 0, 1);
			layer.NoiseLevels.emplace_back(device, vk::ImageViewCreateInfo({}, *layer.Noise, vk::ImageViewType::e2D, Format, {}, mip));
		}
		MakeReadable(Context, *layer.Noise, whole);
		layer.NoiseSlot = Bindless->AddTexture(*layer.NoiseView);

		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageImage, CloudNoiseLevels * 2);
		layer.Pool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo({}, CloudNoiseLevels, poolSize));
		layer.bNoisePending = true;
	}

	void VulkanCloudLayer::MakeTarget(SceneViewCloudLayer& layer, vk::Extent2D extent)
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
		layer.Target = vk::raii::Image(device, imageInfo);
		layer.TargetMemory = AllocateFor(Context, layer.Target);
		layer.Target.bindMemory(*layer.TargetMemory, 0);

		const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		layer.TargetView = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *layer.Target, vk::ImageViewType::e2D, Format, {}, whole));
		const vk::ImageView attachment = *layer.TargetView;
		layer.Framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo({}, *RenderPass, attachment, extent.width, extent.height, 1));
		MakeReadable(Context, *layer.Target, whole);
		layer.TargetSlot = Bindless->AddTexture(attachment);
		layer.Extent = extent;
	}

	void VulkanCloudLayer::ReleaseNoise(SceneViewCloudLayer& layer)
	{
		if (layer.NoiseSlot != 0)
		{
			Bindless->RemoveTexture(layer.NoiseSlot);
		}
		layer.NoiseSlot = 0;
		layer.NoiseSeed = 0;
		layer.bNoisePending = false;
		layer.Pool = nullptr;
		layer.NoiseLevels.clear();
		layer.NoiseView = nullptr;
		layer.Noise = nullptr;
		layer.NoiseMemory = nullptr;
	}

	void VulkanCloudLayer::ReleaseTarget(SceneViewCloudLayer& layer)
	{
		if (layer.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		Bindless->RemoveTexture(layer.TargetSlot);
		layer.TargetSlot = 0;
		layer.Framebuffer = nullptr;
		layer.TargetView = nullptr;
		layer.Target = nullptr;
		layer.TargetMemory = nullptr;
		layer.Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanCloudLayer::RecordNoise(SceneViewCloudLayer& layer, vk::raii::CommandBuffer& cmd)
	{
		if (!layer.bNoisePending || layer.NoiseSlot == 0)
		{
			return;
		}
		vk::raii::Device& device = Context.GetDevice();
		layer.Pool.reset();

		// Written whole, after whatever read it before
		const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, CloudNoiseLevels, 0, 1);
		TransitionImageLayout(*cmd, *layer.Noise, whole, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlags{},
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

		const vk::DescriptorSetLayout setLayout = *NoiseSetLayout;
		for (uint32_t level = 0; level < CloudNoiseLevels; ++level)
		{
			// Freed with the pool when the next ones are recorded
			const vk::DescriptorSet set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*layer.Pool, setLayout)).front().release();
			const vk::DescriptorImageInfo target(nullptr, *layer.NoiseLevels[level], vk::ImageLayout::eGeneral);
			const vk::DescriptorImageInfo previous(nullptr, *layer.NoiseLevels[level == 0 ? 0 : level - 1], vk::ImageLayout::eGeneral);
			const std::array<vk::WriteDescriptorSet, 2> writes = {
				vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eStorageImage, &target),
				vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eStorageImage, &previous),
			};
			device.updateDescriptorSets(writes, {});

			NoiseDispatch dispatch;
			dispatch.Size = CloudNoiseSize >> level;
			dispatch.Seed = layer.NoiseSeed;
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, level == 0 ? *NoisePipeline : *DownsamplePipeline);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *NoisePipelineLayout, 0, { set }, {});
			cmd.pushConstants<NoiseDispatch>(*NoisePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, dispatch);
			cmd.dispatch(Groups(dispatch.Size), Groups(dispatch.Size), 1);

			// What this wrote, the next mip is made from
			const vk::MemoryBarrier written(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, written, {}, {});
		}

		TransitionImageLayout(*cmd, *layer.Noise, whole, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader,
			vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		layer.bNoisePending = false;
		++layer.NoiseBuilds;
	}

	void VulkanCloudLayer::Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		SceneViewCloudLayer& layer = view.CloudLayer;
		if (!layer.IsDrawn())
		{
			return;
		}
		RecordNoise(layer, cmd);

		const vk::Extent2D extent = layer.Extent;
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = *RenderPass;
		begin.framebuffer = *layer.Framebuffer;
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *CloudPipeline);
		BindSceneSets(view, cmd);
		const ObjectConstants object{};
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
}

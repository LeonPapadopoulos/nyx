#include "NyxPCH.h"
#include "VulkanBlur.h"

#include "Paths.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Nyx
{
	namespace
	{
		// The blurred images: 8 bits per channel, like the painter's targets and the scene views
		constexpr vk::Format BlurFormat = vk::Format::eR8G8B8A8Unorm;

		// How many images can be sampled at once: the sources and every level of every target.
		// Six per target with the default options.
		constexpr uint32_t MaxSampleSets = 128;

		// Blur.frag's modes
		constexpr uint32_t ModeDownsample = 0;
		constexpr uint32_t ModeGaussian = 1;

		// BlurPushConstants in Blur.frag
		struct BlurPushConstants
		{
			float SourceTexelWidth = 0.0f;
			float SourceTexelHeight = 0.0f;
			float TargetTexelWidth = 0.0f;
			float TargetTexelHeight = 0.0f;
			float DirectionX = 0.0f;
			float DirectionY = 0.0f;
			uint32_t Mode = 0;
			uint32_t Unused = 0;
		};

		static_assert(sizeof(BlurPushConstants) == 32, "Blur.frag reads exactly 32 bytes of push constants");

		// Half the size, at least one pixel
		vk::Extent2D Halved(vk::Extent2D extent)
		{
			return vk::Extent2D{ std::max(extent.width / 2, 1u), std::max(extent.height / 2, 1u) };
		}
	}

	vk::Image VulkanBlurTarget::GetImage() const
	{
		return Levels.empty() ? vk::Image{} : *Levels.back().Image;
	}

	vk::ImageView VulkanBlurTarget::GetImageView() const
	{
		return Levels.empty() ? vk::ImageView{} : *Levels.back().View;
	}

	vk::Extent2D VulkanBlurTarget::GetExtent() const
	{
		return Levels.empty() ? vk::Extent2D{ 0, 0 } : Levels.back().Extent;
	}

	void VulkanBlurTarget::Release()
	{
		Levels.clear();
		GaussianTemporary = Level{};
		SourceSet = nullptr;
		SourceExtent = vk::Extent2D{ 0, 0 };
	}

	void VulkanBlur::Initialize(Nyx::VulkanContext& context, ShaderPrograms& programs)
	{
		Context = &context;
		Programs = &programs;
		CreateRenderPass();
		CreateDescriptors();
		CreatePipeline();

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		Sampler = vk::raii::Sampler(Context->GetDevice(), samplerInfo);
	}

	void VulkanBlur::RebuildPipeline()
	{
		CreatePipeline();
	}

	void VulkanBlur::Shutdown()
	{
		Sampler = nullptr;
		Pipeline = nullptr;
		PipelineLayout = nullptr;
		DescriptorPool = nullptr;
		SetLayout = nullptr;
		RenderPass = nullptr;
		Context = nullptr;
	}

	void VulkanBlur::Blur(vk::raii::CommandBuffer& commandBuffer, vk::ImageView source, vk::Extent2D sourceExtent, VulkanBlurTarget& target, const BlurOptions& options)
	{
		if (!source || sourceExtent.width == 0 || sourceExtent.height == 0)
		{
			return;
		}
		PrepareTarget(target, source, sourceExtent, options);

		// Halve, each level from the one before
		const vk::raii::DescriptorSet* input = &target.SourceSet;
		vk::Extent2D inputExtent = sourceExtent;
		for (const VulkanBlurTarget::Level& level : target.Levels)
		{
			DrawPass(commandBuffer, *input, inputExtent, level, ModeDownsample, 0.0f, 0.0f);
			input = &level.SampleSet;
			inputExtent = level.Extent;
		}

		// Then the Gaussian at the smallest size: along x into the temporary image, along y back
		const VulkanBlurTarget::Level& result = target.Levels.back();
		for (uint32_t pass = 0; pass < options.GaussianPasses; ++pass)
		{
			DrawPass(commandBuffer, result.SampleSet, result.Extent, target.GaussianTemporary, ModeGaussian, 1.0f, 0.0f);
			DrawPass(commandBuffer, target.GaussianTemporary.SampleSet, result.Extent, result, ModeGaussian, 0.0f, 1.0f);
		}
	}

	void VulkanBlur::DrawPass(vk::raii::CommandBuffer& commandBuffer, const vk::raii::DescriptorSet& input, vk::Extent2D inputExtent, const VulkanBlurTarget::Level& output, uint32_t mode, float directionX, float directionY)
	{
		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = *RenderPass;
		beginInfo.framebuffer = *output.Framebuffer;
		beginInfo.renderArea = vk::Rect2D({ 0, 0 }, output.Extent);
		commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(output.Extent.width), static_cast<float>(output.Extent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D({ 0, 0 }, output.Extent));
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *Pipeline);
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 0, { *input }, {});

		BlurPushConstants push;
		push.SourceTexelWidth = 1.0f / static_cast<float>(inputExtent.width);
		push.SourceTexelHeight = 1.0f / static_cast<float>(inputExtent.height);
		push.TargetTexelWidth = 1.0f / static_cast<float>(output.Extent.width);
		push.TargetTexelHeight = 1.0f / static_cast<float>(output.Extent.height);
		push.DirectionX = directionX;
		push.DirectionY = directionY;
		push.Mode = mode;
		commandBuffer.pushConstants<BlurPushConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { push });

		commandBuffer.draw(3, 1, 0, 0);
		commandBuffer.endRenderPass();
	}

	void VulkanBlur::PrepareTarget(VulkanBlurTarget& target, vk::ImageView source, vk::Extent2D sourceExtent, const BlurOptions& options)
	{
		const BlurOptions sane{ std::max(options.Downsamples, 1u), options.GaussianPasses };
		if (target.Levels.empty() || target.SourceExtent != sourceExtent || target.Options != sane)
		{
			// The renderer waits for the previous frame's GPU work before recording, so the old
			// images are no longer in use.
			target.Release();
			target.Options = sane;
			target.SourceExtent = sourceExtent;

			vk::Extent2D extent = sourceExtent;
			target.Levels.resize(sane.Downsamples);
			for (VulkanBlurTarget::Level& level : target.Levels)
			{
				extent = Halved(extent);
				CreateLevel(level, extent);
			}
			CreateLevel(target.GaussianTemporary, extent);
			++target.Generation;
		}

		// Pointed at the source on every blur, not only when its view seems to change: a source
		// made anew (e.g. a scene view resized) may have been given the handle of the view it
		// replaced, and a set still showing the destroyed view makes the GPU read freed memory.
		if (!*target.SourceSet)
		{
			target.SourceSet = MakeSampleSet(source);
		}
		else
		{
			PointSampleSet(target.SourceSet, source);
		}
	}

	void VulkanBlur::CreateLevel(VulkanBlurTarget::Level& level, vk::Extent2D extent)
	{
		vk::raii::Device& device = Context->GetDevice();
		level.Extent = extent;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = BlurFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		// Drawn into, sampled by the next pass or the UI, and copied out by the GPU tests
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		level.Image = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = level.Image.getMemoryRequirements();
		level.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size,
			FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		level.Image.bindMemory(*level.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *level.Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = BlurFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		level.View = vk::raii::ImageView(device, viewInfo);

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = *RenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &*level.View;
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		level.Framebuffer = vk::raii::Framebuffer(device, framebufferInfo);

		level.SampleSet = MakeSampleSet(*level.View);
	}

	vk::raii::DescriptorSet VulkanBlur::MakeSampleSet(vk::ImageView view)
	{
		vk::raii::Device& device = Context->GetDevice();

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &*SetLayout;
		vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(device, allocInfo).front());
		PointSampleSet(set, view);
		return set;
	}

	void VulkanBlur::PointSampleSet(const vk::raii::DescriptorSet& set, vk::ImageView view)
	{
		vk::DescriptorImageInfo imageInfo{ *Sampler, view, vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::WriteDescriptorSet write{};
		write.dstSet = *set;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &imageInfo;
		Context->GetDevice().updateDescriptorSets(write, {});
	}

	void VulkanBlur::CreateRenderPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = BlurFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		// Every pass writes every pixel of its image.
		colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		std::array<vk::SubpassDependency, 2> dependencies{};
		// Earlier reads of the image (the pass after it, last frame's UI) finish before it is
		// drawn into again.
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = {};
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// The image is read afterwards: by the next pass, the UI, or a copy (the GPU tests).
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();
		RenderPass = vk::raii::RenderPass(Context->GetDevice(), renderPassInfo);
	}

	void VulkanBlur::CreateDescriptors()
	{
		vk::DescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		binding.descriptorCount = 1;
		binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;
		SetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), layoutInfo);

		const vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, MaxSampleSets };
		vk::DescriptorPoolCreateInfo poolInfo{};
		// Targets free their sets when released or resized.
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = MaxSampleSets;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		DescriptorPool = vk::raii::DescriptorPool(Context->GetDevice(), poolInfo);
	}

	void VulkanBlur::CreatePipeline()
	{
		const std::vector<uint32_t>& vertexCode = Programs->GetSpirv("Blur.vert");
		const std::vector<uint32_t>& fragmentCode = Programs->GetSpirv("Blur.frag");
		const vk::raii::ShaderModule vertexModule(Context->GetDevice(), vk::ShaderModuleCreateInfo({}, vertexCode.size() * sizeof(uint32_t), vertexCode.data()));
		const vk::raii::ShaderModule fragmentModule(Context->GetDevice(), vk::ShaderModuleCreateInfo({}, fragmentCode.size() * sizeof(uint32_t), fragmentCode.data()));

		std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
		stages[0].stage = vk::ShaderStageFlagBits::eVertex;
		stages[0].module = *vertexModule;
		stages[0].pName = "main";
		stages[1].stage = vk::ShaderStageFlagBits::eFragment;
		stages[1].module = *fragmentModule;
		stages[1].pName = "main";

		// No vertex buffers: Blur.vert makes its triangle from the vertex index.
		vk::PipelineVertexInputStateCreateInfo vertexInput{};
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// Each pass writes its result as it is, no blending.
		vk::PipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_FALSE;
		blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &blend;

		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(BlurPushConstants) };
		vk::PipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &*SetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstantRange;
		PipelineLayout = vk::raii::PipelineLayout(Context->GetDevice(), layoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *PipelineLayout;
		pipelineInfo.renderPass = *RenderPass;
		pipelineInfo.subpass = 0;
		Pipeline = vk::raii::Pipeline(Context->GetDevice(), Context->GetPipelineCache(), pipelineInfo);
	}
}

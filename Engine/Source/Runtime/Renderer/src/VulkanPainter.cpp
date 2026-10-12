#include "NyxPCH.h"
#include "VulkanPainter.h"

#include "Painter.h"
#include "Paths.h"
#include "VulkanContext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	namespace
	{
		// 8 bits per channel, like the swapchain: the painter blends in the same sRGB-encoded
		// values ImGui does, so the two match side by side.
		constexpr vk::Format TargetFormat = vk::Format::eR8G8B8A8Unorm;

		// How many targets can exist at once; each takes one descriptor set.
		constexpr uint32_t MaxTargets = 32;

		// The binding of each buffer in Painter.frag
		constexpr uint32_t CommandsBinding = 0;
		constexpr uint32_t GroupsBinding = 1;
		constexpr uint32_t TilesBinding = 2;
		constexpr uint32_t EntriesBinding = 3;
		constexpr uint32_t BindingCount = 4;

		// PainterPushConstants in Painter.frag
		struct PainterPushConstants
		{
			uint32_t TilesX = 0;
			uint32_t TilesY = 0;
			uint32_t DebugView = 0;
			uint32_t Unused = 0;
		};

		// Buffers start at this size, so that an empty frame still has valid buffers (Vulkan has
		// no empty ones), and grow by doubling.
		constexpr vk::DeviceSize MinBufferSize = 4096;

		std::vector<uint32_t> ReadSpirvFile(const std::string& fileName)
		{
			const std::filesystem::path path = Nyx::Paths::GetCompiledShadersDir() / fileName;
			std::ifstream file(path, std::ios::ate | std::ios::binary);
			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open SPIR-V file: " + path.string());
			}

			const std::streamsize fileSize = file.tellg();
			if (fileSize <= 0 || (fileSize % sizeof(uint32_t)) != 0)
			{
				throw std::runtime_error("Invalid SPIR-V file size: " + path.string());
			}

			std::vector<uint32_t> spirv(static_cast<size_t>(fileSize) / sizeof(uint32_t));
			file.seekg(0);
			file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
			if (!file)
			{
				throw std::runtime_error("Failed to read SPIR-V file: " + path.string());
			}
			return spirv;
		}

		uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
		{
			const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
			for (uint32_t typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex)
			{
				if ((typeFilter & (1u << typeIndex)) && (memoryProperties.memoryTypes[typeIndex].propertyFlags & properties) == properties)
				{
					return typeIndex;
				}
			}
			throw std::runtime_error("Painter: no suitable memory type");
		}
	}

	void VulkanPainterTarget::Release()
	{
		DescriptorSet = nullptr;
		for (UploadBuffer* buffer : { &Commands, &Groups, &TileBuffer, &Entries })
		{
			buffer->Buffer = nullptr;
			buffer->Memory = nullptr;
			buffer->Mapped = nullptr;
			buffer->Size = 0;
		}
		Framebuffer = nullptr;
		ImageView = nullptr;
		Image = nullptr;
		ImageMemory = nullptr;
		Extent = vk::Extent2D{ 0, 0 };
	}

	void VulkanPainter::Initialize(Nyx::VulkanContext& context)
	{
		Context = &context;
		CheckDeviceLimits();
		CreateRenderPass();
		CreateDescriptorSetLayouts();
		CreateDescriptorPool();
		CreatePipeline();
		CreateEmptyTexture();
	}

	void VulkanPainter::Shutdown()
	{
		EmptyTextureSet = nullptr;
		EmptySampler = nullptr;
		EmptyImageView = nullptr;
		EmptyImage = nullptr;
		EmptyImageMemory = nullptr;
		bEmptyTextureReady = false;

		Pipeline = nullptr;
		PipelineLayout = nullptr;
		DescriptorPool = nullptr;
		TextureSetLayout = nullptr;
		DescriptorSetLayout = nullptr;
		RenderPass = nullptr;
		Context = nullptr;
	}

	void VulkanPainter::Draw(vk::raii::CommandBuffer& commandBuffer, const Painter& painter, VulkanPainterTarget& target, const PainterDrawOptions& options)
	{
		BinPainterCommands(painter, options.Budget, target.Tiles);

		const vk::Extent2D extent{ painter.GetTargetWidth(), painter.GetTargetHeight() };
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}

		PrepareImage(target, extent);
		PrepareEmptyTexture(commandBuffer);
		Upload(target, painter);

		// The clear colour is straight, like every colour given to the painter; the image holds
		// premultiplied colour, like everything the shader writes.
		const glm::vec4 clear = options.ClearColor;
		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a });

		vk::RenderPassBeginInfo beginInfo{};
		beginInfo.renderPass = *RenderPass;
		beginInfo.framebuffer = *target.Framebuffer;
		beginInfo.renderArea = vk::Rect2D({ 0, 0 }, extent);
		beginInfo.clearValueCount = 1;
		beginInfo.pClearValues = &clearValue;
		commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D({ 0, 0 }, extent));

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *Pipeline);
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 0, { *target.DescriptorSet }, {});
		BindTextures(commandBuffer, painter);

		PainterPushConstants push;
		push.TilesX = target.Tiles.TilesX;
		push.TilesY = target.Tiles.TilesY;
		push.DebugView = static_cast<uint32_t>(options.DebugView);
		commandBuffer.pushConstants<PainterPushConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { push });

		// One triangle over the whole target (Painter.vert)
		commandBuffer.draw(3, 1, 0, 0);

		commandBuffer.endRenderPass();
	}

	void VulkanPainter::CreateRenderPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = TargetFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		// Ready to be shown, e.g. as an ImGui image
		colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;

		std::array<vk::SubpassDependency, 2> dependencies{};
		// Earlier reads of the image (last frame's ImGui) finish before it is cleared and painted.
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = {};
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// The painted image is read afterwards, by a shader (ImGui) or a copy (the GPU tests).
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

	void VulkanPainter::CheckDeviceLimits()
	{
		// The commands' set plus one set per texture (PainterCommands.h, PainterMaxTextures)
		const uint32_t setsNeeded = 1 + PainterMaxTextures;
		const uint32_t setsAvailable = Context->GetPhysicalDevice().getProperties().limits.maxBoundDescriptorSets;
		if (setsAvailable < setsNeeded)
		{
			throw std::runtime_error("Painter: the GPU binds " + std::to_string(setsAvailable) + " descriptor sets at once, the painter needs " +
				std::to_string(setsNeeded) + "; lower PainterMaxTextures");
		}
	}

	void VulkanPainter::CreateDescriptorSetLayouts()
	{
		std::array<vk::DescriptorSetLayoutBinding, BindingCount> bindings{};
		for (uint32_t binding = 0; binding < BindingCount; ++binding)
		{
			bindings[binding].binding = binding;
			bindings[binding].descriptorType = vk::DescriptorType::eStorageBuffer;
			bindings[binding].descriptorCount = 1;
			bindings[binding].stageFlags = vk::ShaderStageFlagBits::eFragment;
		}

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		DescriptorSetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), layoutInfo);

		// Exactly as ImGui defines its texture sets, so that its sets are compatible with this one.
		vk::DescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 0;
		textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureBinding.descriptorCount = 1;
		textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo textureLayoutInfo{};
		textureLayoutInfo.bindingCount = 1;
		textureLayoutInfo.pBindings = &textureBinding;
		TextureSetLayout = vk::raii::DescriptorSetLayout(Context->GetDevice(), textureLayoutInfo);
	}

	void VulkanPainter::CreateDescriptorPool()
	{
		// One set per target, plus the empty texture's set. All other textures come with sets of
		// their own, made by ImGui.
		constexpr uint32_t TextureSets = 1;
		const std::array<vk::DescriptorPoolSize, 2> poolSizes = { {
			{ vk::DescriptorType::eStorageBuffer, MaxTargets * BindingCount },
			{ vk::DescriptorType::eCombinedImageSampler, TextureSets },
		} };

		vk::DescriptorPoolCreateInfo poolInfo{};
		// Targets free their set when they are released.
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = MaxTargets + TextureSets;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		DescriptorPool = vk::raii::DescriptorPool(Context->GetDevice(), poolInfo);
	}

	void VulkanPainter::CreateEmptyTexture()
	{
		vk::raii::Device& device = Context->GetDevice();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(1, 1, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = TargetFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		EmptyImage = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = EmptyImage.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		EmptyImageMemory = vk::raii::DeviceMemory(device, allocInfo);
		EmptyImage.bindMemory(*EmptyImageMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *EmptyImage;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = TargetFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		EmptyImageView = vk::raii::ImageView(device, viewInfo);

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eNearest;
		samplerInfo.minFilter = vk::Filter::eNearest;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		EmptySampler = vk::raii::Sampler(device, samplerInfo);

		vk::DescriptorSetAllocateInfo setInfo{};
		setInfo.descriptorPool = *DescriptorPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &*TextureSetLayout;
		EmptyTextureSet = std::move(vk::raii::DescriptorSets(device, setInfo).front());

		vk::DescriptorImageInfo imageDescriptor{ *EmptySampler, *EmptyImageView, vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::WriteDescriptorSet write{};
		write.dstSet = *EmptyTextureSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &imageDescriptor;
		device.updateDescriptorSets(write, {});
	}

	void VulkanPainter::PrepareEmptyTexture(vk::raii::CommandBuffer& commandBuffer)
	{
		if (bEmptyTextureReady)
		{
			return;
		}

		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

		vk::ImageMemoryBarrier toClear{};
		toClear.srcAccessMask = {};
		toClear.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		toClear.oldLayout = vk::ImageLayout::eUndefined;
		toClear.newLayout = vk::ImageLayout::eTransferDstOptimal;
		toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toClear.image = *EmptyImage;
		toClear.subresourceRange = wholeImage;
		commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toClear);

		commandBuffer.clearColorImage(*EmptyImage, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }), wholeImage);

		vk::ImageMemoryBarrier toRead = toClear;
		toRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		toRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, toRead);

		bEmptyTextureReady = true;
	}

	void VulkanPainter::BindTextures(vk::raii::CommandBuffer& commandBuffer, const Painter& painter)
	{
		std::array<vk::DescriptorSet, PainterMaxTextures> textureSets;
		textureSets.fill(*EmptyTextureSet);

		const std::vector<ImTextureRef>& textures = painter.GetTextures();
		for (size_t slot = 0; slot < textures.size() && slot < textureSets.size(); ++slot)
		{
			// With ImGui's Vulkan backend, a texture's ImTextureID is its descriptor set. The font
			// atlas has none until ImGui first uploads it; its glyphs then stay invisible.
			const ImTextureID textureId = textures[slot].GetTexID();
			if (textureId != ImTextureID_Invalid)
			{
				textureSets[slot] = vk::DescriptorSet(reinterpret_cast<VkDescriptorSet>(textureId));
			}
		}

		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *PipelineLayout, 1, textureSets, {});
	}

	void VulkanPainter::CreatePipeline()
	{
		const std::vector<uint32_t> vertexCode = ReadSpirvFile("Painter.vert.spv");
		const std::vector<uint32_t> fragmentCode = ReadSpirvFile("Painter.frag.spv");

		vk::ShaderModuleCreateInfo vertexModuleInfo{};
		vertexModuleInfo.codeSize = vertexCode.size() * sizeof(uint32_t);
		vertexModuleInfo.pCode = vertexCode.data();
		const vk::raii::ShaderModule vertexModule(Context->GetDevice(), vertexModuleInfo);

		vk::ShaderModuleCreateInfo fragmentModuleInfo{};
		fragmentModuleInfo.codeSize = fragmentCode.size() * sizeof(uint32_t);
		fragmentModuleInfo.pCode = fragmentCode.data();
		const vk::raii::ShaderModule fragmentModule(Context->GetDevice(), fragmentModuleInfo);

		std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
		stages[0].stage = vk::ShaderStageFlagBits::eVertex;
		stages[0].module = *vertexModule;
		stages[0].pName = "main";
		stages[1].stage = vk::ShaderStageFlagBits::eFragment;
		stages[1].module = *fragmentModule;
		stages[1].pName = "main";

		// No vertex buffers: Painter.vert makes its triangle from the vertex index.
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

		// The shader blends all of a pixel's commands itself and writes premultiplied colour once;
		// only that result is blended over what the target holds.
		vk::PipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = vk::BlendFactor::eOne;
		blend.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blend.colorBlendOp = vk::BlendOp::eAdd;
		blend.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blend.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blend.alphaBlendOp = vk::BlendOp::eAdd;
		blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &blend;

		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PainterPushConstants);

		// Set 0 holds the commands; sets 1 to PainterMaxTextures one texture each.
		std::array<vk::DescriptorSetLayout, 1 + PainterMaxTextures> setLayouts;
		setLayouts.fill(*TextureSetLayout);
		setLayouts[0] = *DescriptorSetLayout;

		vk::PipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();
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
		Pipeline = vk::raii::Pipeline(Context->GetDevice(), nullptr, pipelineInfo);
	}

	void VulkanPainter::PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent)
	{
		if (*target.Image && target.Extent == extent)
		{
			return;
		}

		// The renderer waits for the previous frame's GPU work before recording, so the old image
		// is no longer in use.
		target.Framebuffer = nullptr;
		target.ImageView = nullptr;
		target.Image = nullptr;
		target.ImageMemory = nullptr;
		target.Extent = extent;

		vk::raii::Device& device = Context->GetDevice();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = TargetFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		// Painted, then shown (sampled) or copied out (the GPU tests)
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		target.Image = vk::raii::Image(device, imageInfo);

		const vk::MemoryRequirements requirements = target.Image.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		target.ImageMemory = vk::raii::DeviceMemory(device, allocInfo);
		target.Image.bindMemory(*target.ImageMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *target.Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = TargetFormat;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		target.ImageView = vk::raii::ImageView(device, viewInfo);

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = *RenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &*target.ImageView;
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		target.Framebuffer = vk::raii::Framebuffer(device, framebufferInfo);
	}

	void VulkanPainter::Upload(VulkanPainterTarget& target, const Painter& painter)
	{
		const PainterTiles& tiles = target.Tiles;
		vk::raii::Device& device = Context->GetDevice();

		// Commands dropped by the budget are always the last ones; nothing points at them.
		const size_t commandCount = painter.GetCommands().size() - tiles.Stats.DroppedCommands;

		struct Source
		{
			VulkanPainterTarget::UploadBuffer& Buffer;
			const void* Data;
			vk::DeviceSize Size;
			uint32_t Binding;
		};
		const std::array<Source, BindingCount> sources = { {
			{ target.Commands, painter.GetCommands().data(), commandCount * sizeof(PainterCommand), CommandsBinding },
			{ target.Groups, painter.GetGroups().data(), painter.GetGroups().size() * sizeof(PainterGroup), GroupsBinding },
			{ target.TileBuffer, tiles.Tiles.data(), tiles.Tiles.size() * sizeof(PainterTile), TilesBinding },
			{ target.Entries, tiles.Entries.data(), tiles.Entries.size() * sizeof(uint32_t), EntriesBinding },
		} };

		if (!*target.DescriptorSet)
		{
			vk::DescriptorSetAllocateInfo allocInfo{};
			allocInfo.descriptorPool = *DescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &*DescriptorSetLayout;
			target.DescriptorSet = std::move(vk::raii::DescriptorSets(device, allocInfo).front());
		}

		for (const Source& source : sources)
		{
			VulkanPainterTarget::UploadBuffer& buffer = source.Buffer;
			if (!*buffer.Buffer || buffer.Size < source.Size)
			{
				// Grow by doubling, so a frame that grows a little each time doesn't reallocate
				// every frame.
				vk::DeviceSize newSize = std::max(buffer.Size, MinBufferSize);
				while (newSize < source.Size)
				{
					newSize *= 2;
				}

				buffer.Mapped = nullptr;
				buffer.Buffer = nullptr;
				buffer.Memory = nullptr;

				vk::BufferCreateInfo bufferInfo{};
				bufferInfo.size = newSize;
				bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
				bufferInfo.sharingMode = vk::SharingMode::eExclusive;
				buffer.Buffer = vk::raii::Buffer(device, bufferInfo);

				// Host-visible and coherent: written by the CPU every frame, and visible to the GPU
				// when the frame's commands are submitted.
				const vk::MemoryRequirements requirements = buffer.Buffer.getMemoryRequirements();
				vk::MemoryAllocateInfo allocInfo{};
				allocInfo.allocationSize = requirements.size;
				allocInfo.memoryTypeIndex = FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits,
					vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
				buffer.Memory = vk::raii::DeviceMemory(device, allocInfo);
				buffer.Buffer.bindMemory(*buffer.Memory, 0);
				buffer.Mapped = buffer.Memory.mapMemory(0, newSize);
				buffer.Size = newSize;

				// The descriptor set points at the buffer, so it follows the new one.
				vk::DescriptorBufferInfo bufferDescriptor{ *buffer.Buffer, 0, VK_WHOLE_SIZE };
				vk::WriteDescriptorSet write{};
				write.dstSet = *target.DescriptorSet;
				write.dstBinding = source.Binding;
				write.descriptorCount = 1;
				write.descriptorType = vk::DescriptorType::eStorageBuffer;
				write.pBufferInfo = &bufferDescriptor;
				device.updateDescriptorSets(write, {});
			}

			if (source.Size > 0)
			{
				std::memcpy(buffer.Mapped, source.Data, static_cast<size_t>(source.Size));
			}
		}
	}
}

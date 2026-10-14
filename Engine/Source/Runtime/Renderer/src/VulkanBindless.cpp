#include "NyxPCH.h"
#include "VulkanBindless.h"

#include "VulkanContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Nyx::Engine
{
	namespace
	{
		// The renderer has one frame in flight; a slot waits a little longer than that to be sure
		constexpr uint32_t FramesBeforeReuse = 3;

		constexpr uint32_t TextureBinding = 0;
		constexpr uint32_t SamplerBinding = 1;
		constexpr uint32_t MaterialBinding = 2;

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
			throw std::runtime_error("Bindless: no suitable memory type");
		}

		std::vector<uint8_t> Filled(uint32_t width, uint32_t height, std::array<uint8_t, 4> color)
		{
			std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
			for (size_t offset = 0; offset < pixels.size(); offset += 4)
			{
				std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
			}
			return pixels;
		}
	}

	void VulkanBindless::Initialize(VulkanContext& context)
	{
		Context = &context;
		CreateSamplers();
		CreateMaterialBuffer();
		CreateSet();
		CreateDefaultTextures();
	}

	void VulkanBindless::Shutdown()
	{
		Set = nullptr;
		Pool = nullptr;
		SetLayout = nullptr;
		Samplers.clear();
		DefaultTextures.clear();
		CreatedTextures.clear();
		FreeMaterialRanges.clear();
		if (MaterialBytes != nullptr)
		{
			MaterialMemory.unmapMemory();
			MaterialBytes = nullptr;
		}
		MaterialBuffer = nullptr;
		MaterialMemory = nullptr;
		MaterialEnd = 0;
		FreeSlots.clear();
		PendingSlots.clear();
		NextSlot = 0;
		Context = nullptr;
	}

	void VulkanBindless::CreateSamplers()
	{
		vk::SamplerCreateInfo linear{};
		linear.magFilter = vk::Filter::eLinear;
		linear.minFilter = vk::Filter::eLinear;
		linear.mipmapMode = vk::SamplerMipmapMode::eLinear;
		linear.addressModeU = vk::SamplerAddressMode::eRepeat;
		linear.addressModeV = vk::SamplerAddressMode::eRepeat;
		linear.addressModeW = vk::SamplerAddressMode::eRepeat;
		linear.maxLod = VK_LOD_CLAMP_NONE;

		vk::SamplerCreateInfo linearClamp = linear;
		linearClamp.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		linearClamp.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		linearClamp.addressModeW = vk::SamplerAddressMode::eClampToEdge;

		vk::SamplerCreateInfo pointClamp = linearClamp;
		pointClamp.magFilter = vk::Filter::eNearest;
		pointClamp.minFilter = vk::Filter::eNearest;
		pointClamp.mipmapMode = vk::SamplerMipmapMode::eNearest;

		vk::SamplerCreateInfo anisotropic = linear;
		anisotropic.anisotropyEnable = VK_TRUE;
		anisotropic.maxAnisotropy = std::min(16.0f, Context->GetPhysicalDevice().getProperties().limits.maxSamplerAnisotropy);

		// In EFixedSampler's order
		for (const vk::SamplerCreateInfo& info : { linear, linearClamp, pointClamp, anisotropic })
		{
			Samplers.emplace_back(Context->GetDevice(), info);
		}
	}

	void VulkanBindless::CreateMaterialBuffer()
	{
		vk::raii::Device& device = Context->GetDevice();
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = MaterialBufferSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		MaterialBuffer = vk::raii::Buffer(device, bufferInfo);
		const vk::MemoryRequirements requirements = MaterialBuffer.getMemoryRequirements();
		MaterialMemory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
		MaterialBuffer.bindMemory(*MaterialMemory, 0);
		MaterialBytes = static_cast<std::byte*>(MaterialMemory.mapMemory(0, MaterialBufferSize));
		std::memset(MaterialBytes, 0, MaterialBufferSize);
	}

	void VulkanBindless::CreateSet()
	{
		vk::raii::Device& device = Context->GetDevice();

		const vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;
		const std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {
			vk::DescriptorSetLayoutBinding(TextureBinding, vk::DescriptorType::eSampledImage, MaxTextures, stages),
			vk::DescriptorSetLayoutBinding(SamplerBinding, vk::DescriptorType::eSampler, FixedSamplerCount, stages),
			vk::DescriptorSetLayoutBinding(MaterialBinding, vk::DescriptorType::eStorageBuffer, 1, stages),
		};
		// Textures come and go while frames that use the set are drawn; slots nobody reads may be empty
		const std::array<vk::DescriptorBindingFlags, 3> bindingFlags = {
			vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound,
			vk::DescriptorBindingFlags{},
			vk::DescriptorBindingFlags{},
		};
		vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
		flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		flagsInfo.pBindingFlags = bindingFlags.data();

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.pNext = &flagsInfo;
		layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		SetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);

		const std::array<vk::DescriptorPoolSize, 3> poolSizes = {
			vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, MaxTextures),
			vk::DescriptorPoolSize(vk::DescriptorType::eSampler, FixedSamplerCount),
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
		};
		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		Pool = vk::raii::DescriptorPool(device, poolInfo);

		vk::DescriptorSetLayout layout = *SetLayout;
		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *Pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;
		Set = std::move(vk::raii::DescriptorSets(device, allocInfo).front());

		std::array<vk::DescriptorImageInfo, FixedSamplerCount> samplerInfos{};
		for (uint32_t index = 0; index < FixedSamplerCount; ++index)
		{
			samplerInfos[index].sampler = *Samplers[index];
		}
		const vk::DescriptorBufferInfo bufferInfo(*MaterialBuffer, 0, MaterialBufferSize);
		const std::array<vk::WriteDescriptorSet, 2> writes = {
			vk::WriteDescriptorSet(*Set, SamplerBinding, 0, FixedSamplerCount, vk::DescriptorType::eSampler, samplerInfos.data()),
			vk::WriteDescriptorSet(*Set, MaterialBinding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo),
		};
		device.updateDescriptorSets(writes, {});
	}

	void VulkanBindless::CreateDefaultTextures()
	{
		// 8x8 magenta and black squares of 2x2 pixels
		std::vector<uint8_t> checker = Filled(8, 8, { 0, 0, 0, 255 });
		for (uint32_t y = 0; y < 8; ++y)
		{
			for (uint32_t x = 0; x < 8; ++x)
			{
				if (((x / 2) + (y / 2)) % 2 == 0)
				{
					const size_t offset = (static_cast<size_t>(y) * 8 + x) * 4;
					checker[offset + 0] = 255;
					checker[offset + 2] = 255;
				}
			}
		}

		// In EBindlessTexture's order, which makes them slots 0 to 3
		constexpr vk::Format Unorm = vk::Format::eR8G8B8A8Unorm;
		DefaultTextures.push_back(MakeTexture(1, 1, Filled(1, 1, { 255, 255, 255, 255 }), Unorm));
		DefaultTextures.push_back(MakeTexture(1, 1, Filled(1, 1, { 0, 0, 0, 255 }), Unorm));
		DefaultTextures.push_back(MakeTexture(1, 1, Filled(1, 1, { 128, 128, 255, 255 }), Unorm));
		DefaultTextures.push_back(MakeTexture(8, 8, checker, Unorm));
		for (const OwnTexture& texture : DefaultTextures)
		{
			AddTexture(*texture.View);
		}
	}

	uint32_t VulkanBindless::CreateTexture(uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, vk::Format format, bool bMips)
	{
		OwnTexture texture = MakeTexture(width, height, pixels, format, 1, bMips);
		const uint32_t slot = AddTexture(*texture.View);
		CreatedTextures[slot] = std::move(texture);
		return slot;
	}

	uint32_t VulkanBindless::CreateCubeTexture(uint32_t size, const std::vector<uint8_t>& pixels, vk::Format format)
	{
		OwnTexture texture = MakeTexture(size, size, pixels, format, 6);
		const uint32_t slot = AddTexture(*texture.View);
		CreatedTextures[slot] = std::move(texture);
		return slot;
	}

	VulkanBindless::OwnTexture VulkanBindless::MakeTexture(uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, vk::Format format, uint32_t layers, bool bMips)
	{
		vk::raii::Device& device = Context->GetDevice();

		// Mips are filtered down with blits, which the format must allow
		const vk::FormatFeatureFlags blitting = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
		const bool bCanBlit = (Context->GetPhysicalDevice().getFormatProperties(format).optimalTilingFeatures & blitting) == blitting;
		const uint32_t levels = bMips && bCanBlit ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1 : 1;

		OwnTexture texture;
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(width, height, 1);
		imageInfo.mipLevels = levels;
		imageInfo.arrayLayers = layers;
		imageInfo.flags = layers == 6 ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{};
		imageInfo.format = format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		texture.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements imageRequirements = texture.Image.getMemoryRequirements();
		texture.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(imageRequirements.size, FindMemoryType(*Context->GetPhysicalDevice(), imageRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		texture.Image.bindMemory(*texture.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *texture.Image;
		viewInfo.viewType = layers == 6 ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, layers);
		texture.View = vk::raii::ImageView(device, viewInfo);

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = pixels.size();
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		vk::raii::Buffer staging(device, bufferInfo);
		const vk::MemoryRequirements bufferRequirements = staging.getMemoryRequirements();
		vk::raii::DeviceMemory stagingMemory(device, vk::MemoryAllocateInfo(bufferRequirements.size, FindMemoryType(*Context->GetPhysicalDevice(), bufferRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
		staging.bindMemory(*stagingMemory, 0);
		std::memcpy(stagingMemory.mapMemory(0, pixels.size()), pixels.data(), pixels.size());
		stagingMemory.unmapMemory();

		vk::CommandBufferAllocateInfo commandBufferInfo{};
		commandBufferInfo.commandPool = *Context->GetGraphicsCommandPool();
		commandBufferInfo.level = vk::CommandBufferLevel::ePrimary;
		commandBufferInfo.commandBufferCount = 1;
		vk::raii::CommandBuffer upload = std::move(vk::raii::CommandBuffers(device, commandBufferInfo).front());
		upload.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		vk::ImageMemoryBarrier toCopy{};
		toCopy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		toCopy.oldLayout = vk::ImageLayout::eUndefined;
		toCopy.newLayout = vk::ImageLayout::eTransferDstOptimal;
		toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy.image = *texture.Image;
		toCopy.subresourceRange = viewInfo.subresourceRange;
		upload.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toCopy);

		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, layers);
		copy.imageExtent = imageInfo.extent;
		upload.copyBufferToImage(*staging, *texture.Image, vk::ImageLayout::eTransferDstOptimal, copy);

		// Each mip from the one before, which is then done
		vk::ImageMemoryBarrier toRead = toCopy;
		toRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		toRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		int32_t levelWidth = static_cast<int32_t>(width);
		int32_t levelHeight = static_cast<int32_t>(height);
		for (uint32_t level = 1; level < levels; ++level)
		{
			vk::ImageMemoryBarrier toSource = toCopy;
			toSource.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, level - 1, 1, 0, layers);
			toSource.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			toSource.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			toSource.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			toSource.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			upload.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toSource);

			const int32_t nextWidth = std::max(levelWidth / 2, 1);
			const int32_t nextHeight = std::max(levelHeight / 2, 1);
			vk::ImageBlit blit{};
			blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level - 1, 0, layers);
			blit.srcOffsets[1] = vk::Offset3D(levelWidth, levelHeight, 1);
			blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, layers);
			blit.dstOffsets[1] = vk::Offset3D(nextWidth, nextHeight, 1);
			upload.blitImage(*texture.Image, vk::ImageLayout::eTransferSrcOptimal, *texture.Image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

			vk::ImageMemoryBarrier done = toSource;
			done.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			done.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			done.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			done.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			upload.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, done);

			levelWidth = nextWidth;
			levelHeight = nextHeight;
		}
		toRead.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, levels - 1, 1, 0, layers);
		upload.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, toRead);
		upload.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*upload;
		Context->GetGraphicsQueue().submit(submitInfo);
		Context->GetGraphicsQueue().waitIdle();
		return texture;
	}

	uint32_t VulkanBindless::AddTexture(vk::ImageView view)
	{
		uint32_t index = 0;
		if (!FreeSlots.empty())
		{
			const auto lowest = std::min_element(FreeSlots.begin(), FreeSlots.end());
			index = *lowest;
			FreeSlots.erase(lowest);
		}
		else if (NextSlot < MaxTextures)
		{
			index = NextSlot++;
		}
		else
		{
			throw std::runtime_error("Bindless: all " + std::to_string(MaxTextures) + " texture slots are taken");
		}

		const vk::DescriptorImageInfo imageInfo(nullptr, view, vk::ImageLayout::eShaderReadOnlyOptimal);
		Context->GetDevice().updateDescriptorSets(vk::WriteDescriptorSet(*Set, TextureBinding, index, 1, vk::DescriptorType::eSampledImage, &imageInfo), {});
		return index;
	}

	void VulkanBindless::RemoveTexture(uint32_t index)
	{
		if (index < static_cast<uint32_t>(DefaultTextures.size()) || index >= NextSlot)
		{
			return; // The defaults stay; an index never handed out is nothing to remove
		}
		PendingSlots.push_back(PendingSlot{ index, FramesBeforeReuse });
	}

	void VulkanBindless::EndFrame()
	{
		for (auto it = PendingSlots.begin(); it != PendingSlots.end();)
		{
			if (--it->FramesLeft > 0)
			{
				++it;
				continue;
			}
			// A material still pointing at the slot shows the missing texture, not a freed image
			const vk::DescriptorImageInfo imageInfo(nullptr, *DefaultTextures[static_cast<size_t>(EBindlessTexture::Missing)].View, vk::ImageLayout::eShaderReadOnlyOptimal);
			Context->GetDevice().updateDescriptorSets(vk::WriteDescriptorSet(*Set, TextureBinding, it->Index, 1, vk::DescriptorType::eSampledImage, &imageInfo), {});
			CreatedTextures.erase(it->Index);
			FreeSlots.push_back(it->Index);
			it = PendingSlots.erase(it);
		}
	}

	std::optional<uint32_t> VulkanBindless::AllocateMaterial(uint32_t size)
	{
		const uint32_t rounded = (size + MaterialAlignment - 1) / MaterialAlignment * MaterialAlignment;
		// Room given back first: the first range it fits in
		for (auto it = FreeMaterialRanges.begin(); size > 0 && it != FreeMaterialRanges.end(); ++it)
		{
			if (it->second >= rounded)
			{
				const uint32_t found = it->first;
				const uint32_t left = it->second - rounded;
				FreeMaterialRanges.erase(it);
				if (left > 0)
				{
					FreeMaterialRanges[found + rounded] = left;
				}
				return found;
			}
		}

		const uint32_t offset = (MaterialEnd + MaterialAlignment - 1) / MaterialAlignment * MaterialAlignment;
		if (size == 0 || offset + static_cast<vk::DeviceSize>(size) > MaterialBufferSize)
		{
			return std::nullopt;
		}
		MaterialEnd = offset + rounded;
		return offset;
	}

	void VulkanBindless::FreeMaterial(uint32_t offset, uint32_t size)
	{
		const uint32_t rounded = (size + MaterialAlignment - 1) / MaterialAlignment * MaterialAlignment;
		if (rounded == 0)
		{
			return;
		}
		auto [it, bInserted] = FreeMaterialRanges.emplace(offset, rounded);
		if (!bInserted)
		{
			return; // Freed twice
		}
		// Joined with its neighbours
		if (const auto next = std::next(it); next != FreeMaterialRanges.end() && it->first + it->second == next->first)
		{
			it->second += next->second;
			FreeMaterialRanges.erase(next);
		}
		if (it != FreeMaterialRanges.begin())
		{
			const auto previous = std::prev(it);
			if (previous->first + previous->second == it->first)
			{
				previous->second += it->second;
				FreeMaterialRanges.erase(it);
			}
		}
	}

	void VulkanBindless::WriteMaterial(uint32_t offset, std::span<const std::byte> bytes)
	{
		if (offset + static_cast<vk::DeviceSize>(bytes.size()) > MaterialBufferSize)
		{
			throw std::runtime_error("Bindless: a material's values would end past the material buffer");
		}
		std::memcpy(MaterialBytes + offset, bytes.data(), bytes.size());
	}
}

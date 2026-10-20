#include "NyxPCH.h"
#include "Texture.h"
#include "Assertions.h"
#include "Log.h"
#include "Paths.h"
#include "VulkanUtil.h"

#include <cstring>
#include <stdexcept>
#include <stb_image.h>

#include <filesystem>
#include <Windows.h>

namespace
{
	void CopyBufferToImage(
		Nyx::VulkanContext& context,
		vk::Buffer buffer,
		vk::Image image,
		uint32_t width,
		uint32_t height)
	{
		vk::raii::CommandBuffer commandBuffer = Nyx::BeginSingleTimeCommands(context);

		vk::BufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = vk::Offset3D{ 0, 0, 0 };
		region.imageExtent = vk::Extent3D{ width, height, 1 };

		commandBuffer.copyBufferToImage(
			buffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			region);

		Nyx::EndSingleTimeCommands(context, std::move(commandBuffer));
	}
}

namespace Nyx
{
	Texture::~Texture()
	{
		Release();
	}

	void Texture::SetContext(VulkanContext& context)
	{
		BoundContext = &context;
	}

	bool Texture::Upload(VulkanContext& context, const ImageData& imageData)
	{
		BoundContext = &context;

		if (imageData.Width <= 0 || imageData.Height <= 0 || imageData.Pixels.empty())
		{
			return false;
		}

		Release();

		Width = imageData.Width;
		Height = imageData.Height;
		Channels = imageData.Channels;

		CreateVulkanImage(context, imageData);
		CreateImageView(context);
		CreateSampler(context);

		return true;
	}

	void Texture::Release()
	{
		Sampler = nullptr;
		ImageView = nullptr;
		Image = nullptr;
		Memory = nullptr;

		Width = 0;
		Height = 0;
		Channels = 0;
	}

	bool Texture::DoLoad()
	{
		ASSERT(BoundContext && "Texture::DoLoad requires a bound VulkanContext. Call SetContext() first.");
		if (!BoundContext)
		{
			return false;
		}

		const std::array<std::string, 3> extensions = {
			".png",
			".jpg",
			".jpeg"
		};

		ImageData imageData;

		for (const std::string& ext : extensions)
		{
			// The project's texture, or the engine's
			const std::filesystem::path filePath = Nyx::Paths::FindAsset(std::filesystem::path("Textures") / (GetId() + ext));

			if (LoadImageData(filePath.string(), imageData))
			{
				return Upload(*BoundContext, imageData);
			}
		}

		return false;
	}

	bool Texture::DoUnload()
	{
		Release();
		return true;
	}

	vk::Image Texture::GetImage() const
	{
		return *Image;
	}

	vk::ImageView Texture::GetImageView() const
	{
		return *ImageView;
	}

	vk::Sampler Texture::GetSampler() const
	{
		return *Sampler;
	}

	int Texture::GetWidth() const
	{
		return Width;
	}

	int Texture::GetHeight() const
	{
		return Height;
	}

	int Texture::GetChannels() const
	{
		return Channels;
	}

	bool Texture::LoadImageData(const std::string& filePath, ImageData& outImageData)
	{
		outImageData = {};

		int width = 0;
		int height = 0;
		int channels = 0;

		stbi_uc* pixels = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels)
		{
			return false;
		}

		const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

		outImageData.Width = width;
		outImageData.Height = height;
		outImageData.Channels = 4;
		outImageData.Pixels.resize(imageSize);
		std::memcpy(outImageData.Pixels.data(), pixels, imageSize);

		stbi_image_free(pixels);
		return true;
	}

	void Texture::CreateVulkanImage(VulkanContext& context, const ImageData& imageData)
	{
		const vk::DeviceSize imageSize =
			static_cast<vk::DeviceSize>(imageData.Width) *
			static_cast<vk::DeviceSize>(imageData.Height) * 4;

		vk::raii::Buffer stagingBuffer{ nullptr };
		vk::raii::DeviceMemory stagingMemory{ nullptr };

		CreateBuffer(
			context,
			imageSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			stagingBuffer,
			stagingMemory);

		void* mapped = stagingMemory.mapMemory(0, imageSize);
		std::memcpy(mapped, imageData.Pixels.data(), static_cast<size_t>(imageSize));
		stagingMemory.unmapMemory();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent.width = static_cast<uint32_t>(imageData.Width);
		imageInfo.extent.height = static_cast<uint32_t>(imageData.Height);
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;

		Image = vk::raii::Image(context.GetDevice(), imageInfo);

		const vk::MemoryRequirements memRequirements = Image.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		Memory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		Image.bindMemory(*Memory, 0);

		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		{
			vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommands(context);
			TransitionImageLayout(*commandBuffer, *Image, wholeImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
				vk::PipelineStageFlagBits::eTopOfPipe, {}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
			EndSingleTimeCommands(context, std::move(commandBuffer));
		}

		CopyBufferToImage(
			context,
			*stagingBuffer,
			*Image,
			static_cast<uint32_t>(imageData.Width),
			static_cast<uint32_t>(imageData.Height));

		{
			vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommands(context);
			TransitionImageLayout(*commandBuffer, *Image, wholeImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
			EndSingleTimeCommands(context, std::move(commandBuffer));
		}
	}

	void Texture::CreateImageView(VulkanContext& context)
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

	void Texture::CreateSampler(VulkanContext& context)
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		Sampler = vk::raii::Sampler(context.GetDevice(), samplerInfo);
	}
}
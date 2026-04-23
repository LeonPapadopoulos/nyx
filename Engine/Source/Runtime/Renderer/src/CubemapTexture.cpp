#include "NyxPCH.h"
#include "CubemapTexture.h"

#include "NyxPCH.h"
#include "CubemapTexture.h"

#include <cstring>
#include <stdexcept>
#include <stb_image.h>

namespace
{
	uint32_t FindMemoryType(
		vk::PhysicalDevice physicalDevice,
		uint32_t typeFilter,
		vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			const bool bTypeMatches = (typeFilter & (1u << i)) != 0;
			const bool bPropertyMatches =
				(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;

			if (bTypeMatches && bPropertyMatches)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable Vulkan memory type.");
	}

	void CreateBuffer(
		Nyx::VulkanContext& context,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::raii::Buffer& outBuffer,
		vk::raii::DeviceMemory& outMemory)
	{
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		outBuffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

		const vk::MemoryRequirements memRequirements = outBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			properties);

		outMemory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		outBuffer.bindMemory(*outMemory, 0);
	}

	vk::raii::CommandBuffer BeginSingleTimeCommands(Nyx::VulkanContext& context)
	{
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *context.GetGraphicsCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		vk::raii::CommandBuffers commandBuffers(context.GetDevice(), allocInfo);
		vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers.front());

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		commandBuffer.begin(beginInfo);
		return commandBuffer;
	}

	void EndSingleTimeCommands(Nyx::VulkanContext& context, vk::raii::CommandBuffer&& commandBuffer)
	{
		commandBuffer.end();

		vk::CommandBuffer rawCmd = *commandBuffer;

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &rawCmd;

		context.GetGraphicsQueue().submit(submitInfo);
		context.GetGraphicsQueue().waitIdle();
	}

	void TransitionCubemapLayout(
		Nyx::VulkanContext& context,
		vk::Image image,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout)
	{
		vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommands(context);

		vk::ImageMemoryBarrier barrier{};
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;

		vk::PipelineStageFlags sourceStage;
		vk::PipelineStageFlags destinationStage;

		if (oldLayout == vk::ImageLayout::eUndefined &&
			newLayout == vk::ImageLayout::eTransferDstOptimal)
		{
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
			newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else
		{
			throw std::runtime_error("Unsupported cubemap image layout transition.");
		}

		commandBuffer.pipelineBarrier(
			sourceStage,
			destinationStage,
			{},
			nullptr,
			nullptr,
			barrier);

		EndSingleTimeCommands(context, std::move(commandBuffer));
	}

	void CopyBufferToCubemap(
		Nyx::VulkanContext& context,
		vk::Buffer buffer,
		vk::Image image,
		uint32_t width,
		uint32_t height,
		vk::DeviceSize faceSize)
	{
		vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommands(context);

		std::array<vk::BufferImageCopy, 6> regions{};

		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
		{
			vk::BufferImageCopy& region = regions[faceIndex];
			region.bufferOffset = faceSize * faceIndex;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = faceIndex;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = vk::Offset3D{ 0, 0, 0 };
			region.imageExtent = vk::Extent3D{ width, height, 1 };
		}

		commandBuffer.copyBufferToImage(
			buffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			regions);

		EndSingleTimeCommands(context, std::move(commandBuffer));
	}
}

namespace Nyx
{
	CubemapTexture::~CubemapTexture()
	{
		Release();
	}

	void CubemapTexture::SetContext(VulkanContext& context)
	{
		BoundContext = &context;
	}

	bool CubemapTexture::Upload(VulkanContext& context, const std::array<ImageData, 6>& faces)
	{
		BoundContext = &context;

		// Basic validation
		if (faces[0].Width <= 0 || faces[0].Height <= 0 || faces[0].Pixels.empty())
		{
			return false;
		}

		const int width = faces[0].Width;
		const int height = faces[0].Height;

		for (uint32_t i = 0; i < 6; ++i)
		{
			if (faces[i].Width != width || faces[i].Height != height)
			{
				return false;
			}

			if (faces[i].Pixels.empty())
			{
				return false;
			}

			// This implementation assumes RGBA8 input data
			const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
			if (faces[i].Pixels.size() != expectedSize)
			{
				return false;
			}
		}

		Release();

		Width = width;
		Height = height;

		CreateCubemapImage(context, faces);
		CreateImageView(context);
		CreateSampler(context);

		return true;
	}

	void CubemapTexture::Release()
	{
		Sampler = nullptr;
		ImageView = nullptr;
		Image = nullptr;
		Memory = nullptr;

		Width = 0;
		Height = 0;
	}

	bool CubemapTexture::DoLoad()
	{
		ASSERT(BoundContext && "CubemapTexture::DoLoad requires a bound VulkanContext. Call SetContext() first.");
		if (!BoundContext)
		{
			return false;
		}

		std::array<ImageData, 6> faces;
		if (!LoadCubemapFaces(faces))
		{
			return false;
		}

		return Upload(*BoundContext, faces);
	}

	bool CubemapTexture::DoUnload()
	{
		Release();
		return true;
	}

	vk::Image CubemapTexture::GetImage() const
	{
		return *Image;
	}

	vk::ImageView CubemapTexture::GetImageView() const
	{
		return *ImageView;
	}

	vk::Sampler CubemapTexture::GetSampler() const
	{
		return *Sampler;
	}

	int CubemapTexture::GetWidth() const
	{
		return Width;
	}

	int CubemapTexture::GetHeight() const
	{
		return Height;
	}

	bool CubemapTexture::LoadFaceImageData(const std::filesystem::path& filePath, ImageData& outImageData)
	{
		outImageData = {};

		int width = 0;
		int height = 0;
		int channels = 0;

		stbi_uc* pixels = stbi_load(filePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
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

	bool CubemapTexture::LoadCubemapFaces(std::array<ImageData, 6>& outFaces)
	{
		const std::filesystem::path cubemapDir =
			Nyx::Paths::GetAssetsDir() / "Textures" / "Skybox" / GetId();

		// We expect the following layouting
		//		  | top	   |
		//	left  |	front  |  right  |	back
		//		  |	bottom |

		const bool bLoaded =
			LoadFaceImageData(cubemapDir / "right.png", outFaces[0]) &&
			LoadFaceImageData(cubemapDir / "left.png", outFaces[1]) &&
			LoadFaceImageData(cubemapDir / "top.png", outFaces[2]) &&
			LoadFaceImageData(cubemapDir / "bottom.png", outFaces[3]) &&
			LoadFaceImageData(cubemapDir / "front.png", outFaces[4]) &&
			LoadFaceImageData(cubemapDir / "back.png", outFaces[5]);

		return bLoaded;
	}

	void CubemapTexture::CreateCubemapImage(VulkanContext& context, const std::array<ImageData, 6>& faces)
	{
		const vk::DeviceSize faceSize =
			static_cast<vk::DeviceSize>(faces[0].Width) *
			static_cast<vk::DeviceSize>(faces[0].Height) * 4;

		const vk::DeviceSize totalSize = faceSize * 6;

		vk::raii::Buffer stagingBuffer{ nullptr };
		vk::raii::DeviceMemory stagingMemory{ nullptr };

		CreateBuffer(
			context,
			totalSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			stagingBuffer,
			stagingMemory);

		void* mapped = stagingMemory.mapMemory(0, totalSize);
		std::uint8_t* dstBytes = static_cast<std::uint8_t*>(mapped);

		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
		{
			std::memcpy(
				dstBytes + faceSize * faceIndex,
				faces[faceIndex].Pixels.data(),
				static_cast<size_t>(faceSize));
		}

		stagingMemory.unmapMemory();

		vk::ImageCreateInfo imageInfo{};
		imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent.width = static_cast<uint32_t>(faces[0].Width);
		imageInfo.extent.height = static_cast<uint32_t>(faces[0].Height);
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 6;
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

		TransitionCubemapLayout(
			context,
			*Image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal);

		CopyBufferToCubemap(
			context,
			*stagingBuffer,
			*Image,
			static_cast<uint32_t>(faces[0].Width),
			static_cast<uint32_t>(faces[0].Height),
			faceSize);

		TransitionCubemapLayout(
			context,
			*Image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	void CubemapTexture::CreateImageView(VulkanContext& context)
	{
		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *Image;
		viewInfo.viewType = vk::ImageViewType::eCube;
		viewInfo.format = Format;
		viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;

		ImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);
	}

	void CubemapTexture::CreateSampler(VulkanContext& context)
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
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
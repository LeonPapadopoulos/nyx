#include "NyxPCH.h"
#include "VulkanTerrainOcclusion.h"

#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <cstring>

namespace Nyx
{
	namespace
	{
		constexpr vk::Format Format = vk::Format::eR8Unorm;
		const vk::ImageSubresourceRange Whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		constexpr size_t Texels = size_t(TerrainOcclusion::Size) * TerrainOcclusion::Size * TerrainOcclusion::Size;
	}

	VulkanTerrainOcclusion::VulkanTerrainOcclusion(VulkanContext& context)
		: Context(context)
	{
	}

	void VulkanTerrainOcclusion::Initialize(VulkanBindless& bindless)
	{
		Bindless = &bindless;
	}

	void VulkanTerrainOcclusion::Prepare(SceneViewTerrainOcclusion& occlusion, const TerrainOcclusionComponent* volume)
	{
		if (volume == nullptr || !volume->bReady || Bindless == nullptr)
		{
			Release(occlusion);
			return;
		}
		// Another volume, such as the terrain's made again: every level copied again
		const bool bNewSource = occlusion.Source != volume;
		occlusion.Source = volume;
		occlusion.Strength = volume->Strength;
		for (int index = 0; index < TerrainOcclusion::Levels; ++index)
		{
			SceneViewTerrainOcclusion::Level& level = occlusion.Levels[size_t(index)];
			const TerrainOcclusionLevel& from = volume->Levels[size_t(index)];
			if (level.Slot == 0)
			{
				MakeLevel(level);
			}
			if (!bNewSource && level.Revision == from.Revision && (level.bWritten || level.bUploadPending))
			{
				continue;
			}
			void* mapped = level.StagingMemory.mapMemory(0, Texels);
			std::memcpy(mapped, from.Solid.data(), Texels);
			level.StagingMemory.unmapMemory();
			level.Revision = from.Revision;
			level.Window = glm::vec4(from.GetWindowMin(), from.CellSize);
			level.bUploadPending = true;
		}
	}

	void VulkanTerrainOcclusion::Release(SceneViewTerrainOcclusion& occlusion)
	{
		for (SceneViewTerrainOcclusion::Level& level : occlusion.Levels)
		{
			if (level.Slot != 0 && Bindless)
			{
				Bindless->RemoveTexture(level.Slot);
			}
		}
		const uint64_t uploads = occlusion.UploadCount;
		occlusion = SceneViewTerrainOcclusion{};
		occlusion.UploadCount = uploads;
	}

	void VulkanTerrainOcclusion::MakeLevel(SceneViewTerrainOcclusion::Level& level)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e3D;
		imageInfo.extent = vk::Extent3D(TerrainOcclusion::Size, TerrainOcclusion::Size, TerrainOcclusion::Size);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		level.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = level.Image.getMemoryRequirements();
		level.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		level.Image.bindMemory(*level.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *level.Image;
		viewInfo.viewType = vk::ImageViewType::e3D;
		viewInfo.format = Format;
		viewInfo.subresourceRange = Whole;
		level.View = vk::raii::ImageView(device, viewInfo);

		CreateBuffer(Context, Texels, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			level.Staging, level.StagingMemory);
		level.Slot = Bindless->AddTexture(*level.View);
		level.bWritten = false;
		level.bUploadPending = false;
	}

	void VulkanTerrainOcclusion::Record(SceneViewTerrainOcclusion& occlusion, vk::raii::CommandBuffer& cmd)
	{
		for (SceneViewTerrainOcclusion::Level& level : occlusion.Levels)
		{
			if (!level.bUploadPending || !*level.Image)
			{
				continue;
			}
			// After the frames before read it in their fragment shaders; what it held is written anew
			TransitionImageLayout(*cmd, *level.Image, Whole, level.bWritten ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlags{}, vk::PipelineStageFlagBits::eTransfer,
				vk::AccessFlagBits::eTransferWrite);
			const vk::BufferImageCopy copy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
				vk::Extent3D(TerrainOcclusion::Size, TerrainOcclusion::Size, TerrainOcclusion::Size));
			cmd.copyBufferToImage(*level.Staging, *level.Image, vk::ImageLayout::eTransferDstOptimal, copy);
			TransitionImageLayout(*cmd, *level.Image, Whole, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
			level.bUploadPending = false;
			level.bWritten = true;
			++occlusion.UploadCount;
		}
	}
}

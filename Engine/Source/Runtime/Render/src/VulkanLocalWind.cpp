#include "NyxPCH.h"
#include "VulkanLocalWind.h"

#include "Profiler.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <cstring>

namespace Nyx
{
	namespace
	{
		// Stored and filtered by every device
		constexpr vk::Format Format = vk::Format::eR16G16B16A16Sfloat;
		const vk::ImageSubresourceRange Whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	}

	VulkanLocalWind::VulkanLocalWind(VulkanContext& context)
		: Context(context)
	{
	}

	void VulkanLocalWind::Initialize(VulkanBindless& bindless)
	{
		Bindless = &bindless;
	}

	void VulkanLocalWind::Prepare(SceneViewLocalWind& wind, const IWindField* field, const glm::vec3& camera, const LocalWindSettings& settings, float time)
	{
		if (!field)
		{
			Release(wind);
			return;
		}
		NYX_PROFILE_SCOPE("VulkanLocalWind::Prepare");
		const WindFieldGrid grid = PlaceLocalWindGrid(camera, settings);
		if (grid.Cells != wind.Grid.Cells || !*wind.Image)
		{
			Release(wind);
			wind.Grid = grid;
			MakeTexture(wind);
		}
		// The same air in the same cells as last time: nothing to fill or copy
		const uint64_t version = field->GetVersion();
		if (version != 0 && version == wind.SampledVersion && field == wind.SampledField && grid.Origin == wind.Grid.Origin &&
			grid.CellSize == wind.Grid.CellSize)
		{
			return;
		}
		wind.Grid = grid;
		wind.SampledField = field;
		wind.SampledVersion = version;
		++wind.SampleCount;

		// The field writes only where it has air
		wind.Velocities.assign(grid.GetCellCount(), glm::vec3(0.0f));
		field->SampleWindGrid(grid, time, wind.Velocities);
		const bool bAny = PackLocalWind(wind.Velocities, wind.Texels);
		wind.Slot = bAny ? wind.TextureSlot : 0;
		wind.bUploadPending = bAny && wind.Texels != wind.Uploaded;
	}

	void VulkanLocalWind::Release(SceneViewLocalWind& wind)
	{
		if (wind.TextureSlot != 0 && Bindless)
		{
			Bindless->RemoveTexture(wind.TextureSlot);
		}
		const uint64_t uploads = wind.UploadCount;
		const uint64_t samples = wind.SampleCount;
		wind = SceneViewLocalWind{};
		wind.UploadCount = uploads;
		wind.SampleCount = samples;
	}

	void VulkanLocalWind::MakeTexture(SceneViewLocalWind& wind)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e3D;
		imageInfo.extent = vk::Extent3D(static_cast<uint32_t>(wind.Grid.Cells.x), static_cast<uint32_t>(wind.Grid.Cells.y), static_cast<uint32_t>(wind.Grid.Cells.z));
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		wind.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = wind.Image.getMemoryRequirements();
		wind.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		wind.Image.bindMemory(*wind.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *wind.Image;
		viewInfo.viewType = vk::ImageViewType::e3D;
		viewInfo.format = Format;
		viewInfo.subresourceRange = Whole;
		wind.View = vk::raii::ImageView(device, viewInfo);

		const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(wind.Grid.GetCellCount()) * 4 * sizeof(uint16_t);
		CreateBuffer(Context, bytes, vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, wind.Staging, wind.StagingMemory);

		wind.TextureSlot = Bindless->AddTexture(*wind.View);
		wind.bWritten = false;
	}

	void VulkanLocalWind::Record(SceneViewLocalWind& wind, vk::raii::CommandBuffer& cmd)
	{
		if (!wind.bUploadPending || !*wind.Image)
		{
			return;
		}
		const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(wind.Texels.size() * sizeof(uint16_t));
		void* mapped = wind.StagingMemory.mapMemory(0, bytes);
		std::memcpy(mapped, wind.Texels.data(), static_cast<size_t>(bytes));
		wind.StagingMemory.unmapMemory();

		// After the frames before read it in their vertex shaders; what it held is written anew
		TransitionImageLayout(*cmd, *wind.Image, Whole, wind.bWritten ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eVertexShader, vk::AccessFlags{}, vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eTransferWrite);
		const vk::BufferImageCopy copy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
			vk::Extent3D(static_cast<uint32_t>(wind.Grid.Cells.x), static_cast<uint32_t>(wind.Grid.Cells.y), static_cast<uint32_t>(wind.Grid.Cells.z)));
		cmd.copyBufferToImage(*wind.Staging, *wind.Image, vk::ImageLayout::eTransferDstOptimal, copy);
		TransitionImageLayout(*cmd, *wind.Image, Whole, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eShaderRead);

		wind.Uploaded = wind.Texels;
		wind.bUploadPending = false;
		wind.bWritten = true;
		++wind.UploadCount;
	}
}

#include "NyxPCH.h"
#include "VulkanColorGrading.h"

#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <glm/gtc/packing.hpp>

#include <cstring>

namespace Nyx
{
	namespace
	{
		constexpr const char* Program = "ColorGrading.slang";
		constexpr const char* EntryPoint = "colorGradingBake";
		// Stored and filtered by every device
		constexpr vk::Format Format = vk::Format::eR16G16B16A16Sfloat;
		// ColorGrading.slang's numthreads, each way
		constexpr uint32_t GroupSize = 4;

		const vk::ImageSubresourceRange Whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	}

	VulkanColorGrading::VulkanColorGrading(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanColorGrading::Initialize(VulkanBindless& bindless)
	{
		Bindless = &bindless;
		vk::raii::Device& device = Context.GetDevice();
		const vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute);
		SetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, binding));
		const vk::DescriptorSetLayout setLayout = *SetLayout;
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ColorGradingConstants));
		PipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, setLayout, pushRange));
		CreatePipelines();
	}

	void VulkanColorGrading::Shutdown()
	{
		BakePipeline = nullptr;
		PipelineLayout = nullptr;
		SetLayout = nullptr;
		Bindless = nullptr;
	}

	void VulkanColorGrading::CreatePipelines()
	{
		const std::vector<uint32_t>& spirv = Programs.GetSpirv(Program, EntryPoint);
		const vk::raii::ShaderModule module = CreateShaderModule(Context, spirv);
		vk::ComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, EntryPoint);
		pipelineInfo.layout = *PipelineLayout;
		BakePipeline = vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo);
	}

	void VulkanColorGrading::Prepare(SceneViewColorGrading& grading, const SceneViewColorGradingSettings& settings)
	{
		if (!settings.bColorGrading)
		{
			Release(grading);
			return;
		}
		if (grading.Slot == 0)
		{
			MakeTable(grading);
		}
		grading.Settings = settings;
		grading.bBakePending = !grading.bBaked || IsColorGradingBakeNeeded(grading.Baked, settings.Constants);
	}

	void VulkanColorGrading::Release(SceneViewColorGrading& grading)
	{
		if (grading.Slot != 0)
		{
			Bindless->RemoveTexture(grading.Slot);
		}
		const uint32_t bakes = grading.BakeCount;
		grading = SceneViewColorGrading{};
		grading.BakeCount = bakes;
	}

	void VulkanColorGrading::Rebake(SceneViewColorGrading& grading)
	{
		grading.bBaked = false;
	}

	void VulkanColorGrading::MakeTable(SceneViewColorGrading& grading)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e3D;
		imageInfo.extent = vk::Extent3D(ColorGradingTableSize, ColorGradingTableSize, ColorGradingTableSize);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		grading.Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = grading.Image.getMemoryRequirements();
		grading.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		grading.Image.bindMemory(*grading.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *grading.Image;
		viewInfo.viewType = vk::ImageViewType::e3D;
		viewInfo.format = Format;
		viewInfo.subresourceRange = Whole;
		grading.View = vk::raii::ImageView(device, viewInfo);

		// Its own set, written once: the table as the bake's storage image
		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageImage, 1);
		grading.Pool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo({}, 1, poolSize));
		const vk::DescriptorSetLayout setLayout = *SetLayout;
		grading.Set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*grading.Pool, setLayout)).front().release();
		const vk::DescriptorImageInfo table(nullptr, *grading.View, vk::ImageLayout::eGeneral);
		device.updateDescriptorSets(vk::WriteDescriptorSet(grading.Set, 0, 0, 1, vk::DescriptorType::eStorageImage, &table), {});

		grading.Slot = Bindless->AddTexture(*grading.View);
		grading.bBaked = false;
	}

	void VulkanColorGrading::Bake(SceneViewColorGrading& grading, vk::raii::CommandBuffer& cmd)
	{
		if (grading.Slot == 0 || !grading.bBakePending)
		{
			return;
		}
		// After the frames before read it in their tonemap; what it held is written anew
		TransitionImageLayout(*cmd, *grading.Image, Whole, grading.bBaked ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlags{}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite);

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *BakePipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *PipelineLayout, 0, grading.Set, {});
		cmd.pushConstants<ColorGradingConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, grading.Settings.Constants);
		constexpr uint32_t Groups = (ColorGradingTableSize + GroupSize - 1) / GroupSize;
		cmd.dispatch(Groups, Groups, Groups);

		TransitionImageLayout(*cmd, *grading.Image, Whole, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		grading.Baked = grading.Settings.Constants;
		grading.bBaked = true;
		grading.bBakePending = false;
		++grading.BakeCount;
	}

	std::vector<glm::vec4> VulkanColorGrading::ReadTable(const SceneViewColorGrading& grading)
	{
		if (grading.Slot == 0 || !grading.bBaked)
		{
			return {};
		}
		Context.WaitIdle();
		constexpr size_t Texels = static_cast<size_t>(ColorGradingTableSize) * ColorGradingTableSize * ColorGradingTableSize;
		vk::raii::Buffer readback{ nullptr };
		vk::raii::DeviceMemory readbackMemory{ nullptr };
		CreateBuffer(Context, Texels * 4 * sizeof(uint16_t), vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, readback, readbackMemory);

		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		TransitionImageLayout(*commands, *grading.Image, Whole, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copy.imageExtent = vk::Extent3D(ColorGradingTableSize, ColorGradingTableSize, ColorGradingTableSize);
		commands.copyImageToBuffer(*grading.Image, vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
		TransitionImageLayout(*commands, *grading.Image, Whole, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));

		std::vector<uint16_t> halves(Texels * 4);
		std::memcpy(halves.data(), readbackMemory.mapMemory(0, halves.size() * sizeof(uint16_t)), halves.size() * sizeof(uint16_t));
		readbackMemory.unmapMemory();
		std::vector<glm::vec4> texels(Texels);
		for (size_t texel = 0; texel < Texels; ++texel)
		{
			texels[texel] = glm::vec4(glm::unpackHalf1x16(halves[texel * 4]), glm::unpackHalf1x16(halves[texel * 4 + 1]), glm::unpackHalf1x16(halves[texel * 4 + 2]), glm::unpackHalf1x16(halves[texel * 4 + 3]));
		}
		return texels;
	}
}

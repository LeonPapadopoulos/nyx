#include "NyxPCH.h"
#include "VulkanEnvironment.h"

#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "Log.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Nyx
{
	namespace
	{
		constexpr const char* Program = "Environment.slang";
		constexpr vk::Format Format = vk::Format::eR16G16B16A16Sfloat;
		constexpr uint32_t PartialThreads = 256;

		// EnvironmentConstants in Environment.slang
		struct EnvironmentConstants
		{
			uint32_t Source = 0;
			uint32_t Size = 0;
			uint32_t SourceSize = 0;
			float Roughness = 0.0f;
			uint32_t SampleCount = 0;
			float SourceLevel = 0.0f;
			uint32_t Padding[2] = {};
		};
		static_assert(sizeof(EnvironmentConstants) == 32, "EnvironmentConstants must match Environment.slang");

		uint32_t Groups(uint32_t size)
		{
			return (size + 7) / 8;
		}

		// A host-visible buffer, for reading back
		struct HostBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };

			HostBuffer(Nyx::VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage)
			{
				CreateBuffer(context, size, usage, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, Buffer, Memory);
			}
		};
	}

	void VulkanEnvironment::Initialize(Nyx::VulkanContext& context, VulkanBindless& bindless, ShaderPrograms& programs)
	{
		Context = &context;
		Bindless = &bindless;
		Programs = &programs;

		vk::raii::Device& device = Context->GetDevice();
		const std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
			vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
		};
		SetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));

		// Set 0 is the bindless set, for the sky and the source's mips
		const std::array<vk::DescriptorSetLayout, 2> setLayouts = { *Bindless->GetSetLayout(), *SetLayout };
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(EnvironmentConstants));
		PipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, setLayouts, pushRange));

		// A set per dispatch, all freed at each build: a build has fewer than 32 dispatches
		const std::array<vk::DescriptorPoolSize, 2> poolSizes = {
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 96),
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 32),
		};
		Pool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo({}, 32, poolSizes));

		CreatePipelines();
		BuildLookup();
	}

	void VulkanEnvironment::Shutdown()
	{
		if (!Context)
		{
			return;
		}
		Context->WaitIdle();
		for (auto& [sky, light] : Lights)
		{
			Release(light.Specular);
		}
		Lights.clear();
		Release(Lookup);
		Pipelines.clear();
		Pool = nullptr;
		PipelineLayout = nullptr;
		SetLayout = nullptr;
		Context = nullptr;
	}

	void VulkanEnvironment::Build(uint32_t skySlot)
	{
		BuildLight(skySlot, Lights[skySlot]);
	}

	void VulkanEnvironment::Forget(uint32_t skySlot)
	{
		const auto found = Lights.find(skySlot);
		if (found == Lights.end())
		{
			return;
		}
		// Frames in flight may still read it
		Context->WaitIdle();
		Release(found->second.Specular);
		Lights.erase(found);
	}

	void VulkanEnvironment::Rebuild()
	{
		Context->WaitIdle();
		CreatePipelines();
		BuildLookup();
		for (auto& [sky, light] : Lights)
		{
			BuildLight(sky, light);
		}
	}

	uint32_t VulkanEnvironment::GetSpecularSlot(uint32_t skySlot) const
	{
		const auto found = Lights.find(skySlot);
		return found != Lights.end() ? found->second.Specular.Slot : 0;
	}

	const std::array<glm::vec3, 9>& VulkanEnvironment::GetIrradiance(uint32_t skySlot) const
	{
		static const std::array<glm::vec3, 9> None{};
		const auto found = Lights.find(skySlot);
		return found != Lights.end() ? found->second.Irradiance : None;
	}

	void VulkanEnvironment::CreatePipelines()
	{
		Pipelines.clear();
		for (const char* entryPoint : { "environmentCopy", "environmentDownsample", "environmentPrefilter", "environmentIrradiance", "brdfLookup" })
		{
			const std::vector<uint32_t>& spirv = Programs->GetSpirv(Program, entryPoint);
			vk::raii::ShaderModule module(Context->GetDevice(), vk::ShaderModuleCreateInfo({}, spirv.size() * sizeof(uint32_t), spirv.data()));
			vk::ComputePipelineCreateInfo pipelineInfo{};
			pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint);
			pipelineInfo.layout = *PipelineLayout;
			Pipelines.emplace(entryPoint, vk::raii::Pipeline(Context->GetDevice(), Context->GetPipelineCache(), pipelineInfo));
		}
	}

	void VulkanEnvironment::BuildLookup()
	{
		Release(Lookup);
		Lookup = MakeImage(LookupSize, 1, 1);
		Pool.reset();
		RunCommands([this](vk::raii::CommandBuffer& commands)
			{
				Transition(commands, Lookup, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
				EnvironmentConstants constants;
				constants.Size = LookupSize;
				constants.SampleCount = LookupSamples;
				Bindings bindings;
				bindings.Lookup = *Lookup.Levels[0];
				Dispatch(commands, "brdfLookup", &constants, bindings, Groups(LookupSize), Groups(LookupSize), 1);
				Transition(commands, Lookup, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal);
			});
		Lookup.Slot = Bindless->AddTexture(*Lookup.Sampled);
		Lookup.bHasSlot = true;
	}

	void VulkanEnvironment::BuildLight(uint32_t skySlot, Light& light)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		Context->WaitIdle();
		Release(light.Specular);
		Image source = MakeImage(SourceSize, 10, 6);
		Image& specular = light.Specular;
		specular = MakeImage(SpecularSize, SpecularLevels, 6);
		Pool.reset();

		// The source's mips are made before the other dispatches read them through the array
		source.Slot = Bindless->AddTexture(*source.Sampled);
		source.bHasSlot = true;

		HostBuffer partials(*Context, PartialThreads * 9 * sizeof(glm::vec4), vk::BufferUsageFlagBits::eStorageBuffer);
		RunCommands([&](vk::raii::CommandBuffer& commands)
			{
				Transition(commands, source, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
				Transition(commands, specular, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

				// The sky in HDR, with every mip
				EnvironmentConstants constants;
				constants.Source = skySlot;
				constants.Size = SourceSize;
				Bindings bindings;
				bindings.Target = *source.Levels[0];
				Dispatch(commands, "environmentCopy", &constants, bindings, Groups(SourceSize), Groups(SourceSize), 6);
				for (uint32_t level = 1; level < source.LevelCount; ++level)
				{
					constants.Size = SourceSize >> level;
					bindings.Target = *source.Levels[level];
					bindings.Previous = *source.Levels[level - 1];
					Dispatch(commands, "environmentDownsample", &constants, bindings, Groups(constants.Size), Groups(constants.Size), 6);
				}
				Transition(commands, source, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal);

				// The specular cube, rougher with each mip
				for (uint32_t level = 0; level < SpecularLevels; ++level)
				{
					EnvironmentConstants prefilter;
					prefilter.Source = source.Slot;
					prefilter.Size = SpecularSize >> level;
					prefilter.SourceSize = SourceSize;
					prefilter.Roughness = static_cast<float>(level) / static_cast<float>(SpecularLevels - 1);
					prefilter.SampleCount = level == 0 ? 1 : SpecularSamples;
					// Mip 0 is a mirror: the source's mip of its size
					prefilter.SourceLevel = std::log2(static_cast<float>(SourceSize) / static_cast<float>(SpecularSize));
					Bindings target;
					target.Target = *specular.Levels[level];
					Dispatch(commands, "environmentPrefilter", &prefilter, target, Groups(prefilter.Size), Groups(prefilter.Size), 6);
				}
				Transition(commands, specular, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal);

				// The irradiance, summed in parts
				EnvironmentConstants irradiance;
				irradiance.Source = source.Slot;
				irradiance.Size = SourceSize >> IrradianceLevel;
				irradiance.SourceLevel = static_cast<float>(IrradianceLevel);
				Bindings sums;
				sums.Partials = *partials.Buffer;
				Dispatch(commands, "environmentIrradiance", &irradiance, sums, 1, 1, 1);
				const vk::MemoryBarrier toHost(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead);
				commands.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost, {}, toHost, {}, {});
			});
		specular.Slot = Bindless->AddTexture(*specular.Sampled);
		specular.bHasSlot = true;
		// Read by the dispatches above only, which are done
		Release(source);

		// The parts added up, then convolved with the cosine and divided by pi: A0 = pi,
		// A1 = 2 pi / 3, A2 = pi / 4 (Ramamoorthi and Hanrahan)
		std::vector<glm::vec4> parts(PartialThreads * 9);
		std::memcpy(parts.data(), partials.Memory.mapMemory(0, parts.size() * sizeof(glm::vec4)), parts.size() * sizeof(glm::vec4));
		partials.Memory.unmapMemory();
		constexpr std::array<float, 9> Convolution = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
		for (uint32_t term = 0; term < 9; ++term)
		{
			glm::vec3 sum(0.0f);
			for (uint32_t thread = 0; thread < PartialThreads; ++thread)
			{
				sum += glm::vec3(parts[thread * 9 + term]);
			}
			light.Irradiance[term] = sum * Convolution[term];
		}
		LOG_INFO("The sky's light made in {:.0f} ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}

	VulkanEnvironment::Image VulkanEnvironment::MakeImage(uint32_t size, uint32_t levels, uint32_t layers)
	{
		vk::raii::Device& device = Context->GetDevice();
		Image image;
		image.Size = size;
		image.LevelCount = levels;
		image.Layers = layers;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.flags = layers == 6 ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(size, size, 1);
		imageInfo.mipLevels = levels;
		imageInfo.arrayLayers = layers;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		image.Handle = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = image.Handle.getMemoryRequirements();
		image.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		image.Handle.bindMemory(*image.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *image.Handle;
		viewInfo.viewType = layers == 6 ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
		viewInfo.format = Format;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, layers);
		image.Sampled = vk::raii::ImageView(device, viewInfo);

		// Storage views: a cube's mip as an array of its six faces
		for (uint32_t level = 0; level < levels; ++level)
		{
			viewInfo.viewType = layers == 6 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
			viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, level, 1, 0, layers);
			image.Levels.emplace_back(device, viewInfo);
		}
		return image;
	}

	void VulkanEnvironment::Release(Image& image)
	{
		if (image.bHasSlot)
		{
			Bindless->RemoveTexture(image.Slot);
		}
		image = Image{};
	}

	void VulkanEnvironment::Transition(vk::raii::CommandBuffer& commands, const Image& image, vk::ImageLayout from, vk::ImageLayout to)
	{
		const bool bFresh = from == vk::ImageLayout::eUndefined;
		const vk::PipelineStageFlags dstStages = to == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader
																							   : vk::PipelineStageFlagBits::eComputeShader;
		TransitionImageLayout(*commands, *image.Handle, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, image.LevelCount, 0, image.Layers), from, to,
			bFresh ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eComputeShader, bFresh ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderWrite,
			dstStages, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	}

	void VulkanEnvironment::Dispatch(vk::raii::CommandBuffer& commands, const std::string& entryPoint, const void* constants, const Bindings& bindings, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
	{
		vk::raii::Device& device = Context->GetDevice();
		vk::DescriptorSetLayout layout = *SetLayout;
		// Freed with the pool at the next build
		const vk::DescriptorSet set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*Pool, layout)).front().release();

		std::vector<vk::WriteDescriptorSet> writes;
		const vk::DescriptorImageInfo target(nullptr, bindings.Target, vk::ImageLayout::eGeneral);
		const vk::DescriptorImageInfo previous(nullptr, bindings.Previous, vk::ImageLayout::eGeneral);
		const vk::DescriptorBufferInfo partials(bindings.Partials, 0, VK_WHOLE_SIZE);
		const vk::DescriptorImageInfo lookup(nullptr, bindings.Lookup, vk::ImageLayout::eGeneral);
		if (bindings.Target)
		{
			writes.emplace_back(set, 0, 0, 1, vk::DescriptorType::eStorageImage, &target);
		}
		if (bindings.Previous)
		{
			writes.emplace_back(set, 1, 0, 1, vk::DescriptorType::eStorageImage, &previous);
		}
		if (bindings.Partials)
		{
			writes.emplace_back(set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &partials);
		}
		if (bindings.Lookup)
		{
			writes.emplace_back(set, 3, 0, 1, vk::DescriptorType::eStorageImage, &lookup);
		}
		device.updateDescriptorSets(writes, {});

		commands.bindPipeline(vk::PipelineBindPoint::eCompute, *Pipelines.at(entryPoint));
		commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *PipelineLayout, 0, { Bindless->GetSet(), set }, {});
		commands.pushConstants<EnvironmentConstants>(*PipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, *static_cast<const EnvironmentConstants*>(constants));
		commands.dispatch(groupsX, groupsY, groupsZ);

		// What this wrote, the next dispatch reads
		const vk::MemoryBarrier written(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		commands.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, written, {}, {});
	}

	void VulkanEnvironment::RunCommands(const std::function<void(vk::raii::CommandBuffer&)>& record)
	{
		vk::raii::Device& device = Context->GetDevice();
		vk::raii::CommandBuffer commands = std::move(vk::raii::CommandBuffers(device, vk::CommandBufferAllocateInfo(*Context->GetGraphicsCommandPool(), vk::CommandBufferLevel::ePrimary, 1)).front());
		commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		record(commands);
		commands.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commands;
		Context->GetGraphicsQueue().submit(submitInfo);
		Context->GetGraphicsQueue().waitIdle();
	}

	std::vector<glm::vec4> VulkanEnvironment::ReadImage(Image& image, uint32_t level)
	{
		const uint32_t size = std::max(image.Size >> level, 1u);
		const size_t texels = static_cast<size_t>(size) * size * image.Layers;
		HostBuffer readback(*Context, texels * 8, vk::BufferUsageFlagBits::eTransferDst);
		RunCommands([&](vk::raii::CommandBuffer& commands)
			{
				const vk::ImageSubresourceRange levelRange(vk::ImageAspectFlagBits::eColor, level, 1, 0, image.Layers);
				TransitionImageLayout(*commands, *image.Handle, levelRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
					vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead,
					vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);

				vk::BufferImageCopy copy{};
				copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, image.Layers);
				copy.imageExtent = vk::Extent3D(size, size, 1);
				commands.copyImageToBuffer(*image.Handle, vk::ImageLayout::eTransferSrcOptimal, *readback.Buffer, copy);

				TransitionImageLayout(*commands, *image.Handle, levelRange, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead,
					vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
			});

		std::vector<uint16_t> halves(texels * 4);
		std::memcpy(halves.data(), readback.Memory.mapMemory(0, halves.size() * sizeof(uint16_t)), halves.size() * sizeof(uint16_t));
		readback.Memory.unmapMemory();
		std::vector<glm::vec4> values(texels);
		for (size_t texel = 0; texel < texels; ++texel)
		{
			values[texel] = glm::vec4(glm::unpackHalf1x16(halves[texel * 4]), glm::unpackHalf1x16(halves[texel * 4 + 1]), glm::unpackHalf1x16(halves[texel * 4 + 2]), glm::unpackHalf1x16(halves[texel * 4 + 3]));
		}
		return values;
	}

	std::vector<glm::vec4> VulkanEnvironment::ReadSpecularLevel(uint32_t skySlot, uint32_t level)
	{
		return ReadImage(Lights.at(skySlot).Specular, level);
	}

	std::vector<glm::vec2> VulkanEnvironment::ReadLookup()
	{
		const std::vector<glm::vec4> values = ReadImage(Lookup, 0);
		std::vector<glm::vec2> lookup(values.size());
		std::transform(values.begin(), values.end(), lookup.begin(), [](const glm::vec4& value)
			{
				return glm::vec2(value);
			});
		return lookup;
	}
}

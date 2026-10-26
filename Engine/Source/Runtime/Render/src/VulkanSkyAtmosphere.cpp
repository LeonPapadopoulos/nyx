#include "NyxPCH.h"
#include "VulkanSkyAtmosphere.h"

#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include "backends/imgui_impl_vulkan.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>

namespace Nyx
{
	namespace
	{
		constexpr const char* Program = "SkyAtmosphere.slang";
		constexpr vk::Format TableFormat = vk::Format::eR16G16B16A16Sfloat;
		constexpr vk::Format PreviewFormat = vk::Format::eR8G8B8A8Unorm;
		// The most dispatches one frame records for a view: every table, the cube's mips, the
		// specular cube's, and the pictures
		constexpr uint32_t MaxDispatches = 24;

		// SkyConstants in SkyAtmosphere.slang
		struct SkyConstants
		{
			glm::vec4 RayleighScattering{ 0.0f };
			glm::vec4 MieScattering{ 0.0f };
			glm::vec4 MieExtinction{ 0.0f };
			glm::vec4 OzoneAbsorption{ 0.0f };
			glm::vec4 GroundAlbedo{ 0.0f };
			glm::vec4 TowardsSun{ 0.0f };
			glm::vec4 SunIlluminance{ 0.0f };
			glm::vec4 WarmthTint{ 0.0f };
			glm::vec4 NightLight{ 0.0f };
			uint32_t TransmittanceLut = 0;
			uint32_t MultipleScatteringLut = 0;
			uint32_t SkyViewLut = 0;
			uint32_t SkyCube = 0;
			// The cloud layer the cube draws: 1 while it does; its noise; how much of it
			uint32_t CloudLayer = 0;
			uint32_t CloudNoise = 0;
			float CloudStrength = 0.0f;
			uint32_t CloudPadding = 0;
			glm::vec4 CloudShell{ 0.0f };
			glm::vec4 CloudShape{ 0.0f };
			glm::vec4 CloudWind{ 0.0f };
			glm::vec4 CloudOrigin{ 0.0f };
		};
		static_assert(sizeof(SkyConstants) == 240, "SkyConstants must match SkyAtmosphere.slang");

		// SkyDispatch in SkyAtmosphere.slang
		struct SkyDispatch
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Source = 0;
			uint32_t Mode = 0;
			float Roughness = 0.0f;
			float SourceLevel = 0.0f;
			float SourceSize = 0.0f;
			float Exposure = 1.0f;
		};
		static_assert(sizeof(SkyDispatch) == 32, "SkyDispatch must match SkyAtmosphere.slang");

		uint32_t Groups(uint32_t size)
		{
			return (size + 7) / 8;
		}

		// The pictures' sizes: the tables', the small one four times bigger, the cube's six faces
		// unfolded as a cross
		constexpr std::array<glm::uvec2, 4> PreviewSizes = { glm::uvec2(VulkanSkyAtmosphere::TransmittanceWidth, VulkanSkyAtmosphere::TransmittanceHeight),
			glm::uvec2(VulkanSkyAtmosphere::MultipleScatteringSize * 4), glm::uvec2(VulkanSkyAtmosphere::SkyViewWidth, VulkanSkyAtmosphere::SkyViewHeight),
			glm::uvec2(VulkanSkyAtmosphere::CubeSize * 4, VulkanSkyAtmosphere::CubeSize * 3) };
	}

	VulkanSkyAtmosphere::VulkanSkyAtmosphere(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanSkyAtmosphere::Initialize(VulkanBindless& bindless)
	{
		Bindless = &bindless;
		vk::raii::Device& device = Context.GetDevice();
		const std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
			vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
		};
		SetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));

		// Set 0 is the bindless set, for the tables made before
		const std::array<vk::DescriptorSetLayout, 2> setLayouts = { *Bindless->GetSetLayout(), *SetLayout };
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(SkyDispatch));
		PipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, setLayouts, pushRange));

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		PreviewSampler = vk::raii::Sampler(device, samplerInfo);
		CreatePipelines();
	}

	void VulkanSkyAtmosphere::Shutdown()
	{
		Pipelines.clear();
		PreviewSampler = nullptr;
		PipelineLayout = nullptr;
		SetLayout = nullptr;
		Bindless = nullptr;
	}

	void VulkanSkyAtmosphere::CreatePipelines()
	{
		Pipelines.clear();
		for (const char* entryPoint :
			{ "skyTransmittance", "skyMultipleScattering", "skySkyView", "skyCube", "skyCubeDownsample", "skyIrradiance", "skySpecular", "skyPreview" })
		{
			vk::raii::ShaderModule module = CreateShaderModule(Context, Programs.GetSpirv(Program, entryPoint));
			vk::ComputePipelineCreateInfo pipelineInfo{};
			pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint);
			pipelineInfo.layout = *PipelineLayout;
			Pipelines.emplace(entryPoint, vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo));
		}
	}

	void VulkanSkyAtmosphere::Prepare(SceneViewSkyTables& tables, const SceneViewSkyAtmosphere& wanted, bool bPreviews, float previewExposure, const SceneViewCloudCapture& clouds)
	{
		if (!wanted.bAtmosphere)
		{
			Release(tables);
			return;
		}
		if (!tables.IsMade())
		{
			MakeTables(tables);
		}
		if (bPreviews && !tables.Previews.front().bHasSlot)
		{
			for (size_t preview = 0; preview < tables.Previews.size(); ++preview)
			{
				tables.Previews[preview] = MakeImage(PreviewSizes[preview].x, PreviewSizes[preview].y, 1, 1, PreviewFormat);
			}
		}

		// What changed since they were made, and anything not made since it last changed
		const SkyAtmosphereChanges changes = tables.bBuilt ? GetSkyAtmosphereChanges(tables.Built, wanted) : SkyAtmosphereChanges{ true, true };
		tables.Pending.bAir = tables.Pending.bAir || changes.bAir;
		tables.Pending.bSkyView = tables.Pending.bSkyView || changes.bSkyView;
		tables.bPreviews = bPreviews;
		tables.PreviewExposure = previewExposure;
		if (changes.bAir || changes.bSkyView)
		{
			tables.Built = wanted;
			tables.bBuilt = true;
		}
		// The clouds in the cube: made again with it whenever it is, and alone when they moved
		const bool bCube = tables.Pending.bAir || tables.Pending.bSkyView;
		if (bCube || IsCloudCaptureChanged(tables.BuiltClouds, clouds))
		{
			tables.BuiltClouds = clouds;
			tables.bCubePending = true;
		}
		if (tables.Pending.bAir || tables.Pending.bSkyView || tables.bCubePending)
		{
			WriteConstants(tables);
		}
	}

	void VulkanSkyAtmosphere::MakeTables(SceneViewSkyTables& tables)
	{
		tables.Transmittance = MakeImage(TransmittanceWidth, TransmittanceHeight, 1, 1, TableFormat);
		tables.MultipleScattering = MakeImage(MultipleScatteringSize, MultipleScatteringSize, 1, 1, TableFormat);
		tables.SkyView = MakeImage(SkyViewWidth, SkyViewHeight, 1, 1, TableFormat);
		tables.Cube = MakeImage(CubeSize, CubeSize, CubeLevels, 6, TableFormat);
		tables.Irradiance = MakeImage(IrradianceSize, IrradianceSize, 1, 6, TableFormat);
		tables.Specular = MakeImage(CubeSize, CubeSize, SpecularLevels, 6, TableFormat);

		CreateBuffer(Context, sizeof(SkyConstants), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			tables.Constants, tables.ConstantsMemory);
		const std::array<vk::DescriptorPoolSize, 2> poolSizes = {
			vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MaxDispatches),
			vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, MaxDispatches * 3),
		};
		tables.Pool = vk::raii::DescriptorPool(Context.GetDevice(), vk::DescriptorPoolCreateInfo({}, MaxDispatches, poolSizes));
		tables.bBuilt = false;
		tables.Pending = {};
	}

	void VulkanSkyAtmosphere::WriteConstants(SceneViewSkyTables& tables)
	{
		const SceneViewSkyAtmosphere& sky = tables.Built;
		SkyConstants constants;
		constants.RayleighScattering = glm::vec4(sky.RayleighScattering, sky.RayleighHeight);
		constants.MieScattering = glm::vec4(sky.MieScattering, sky.MieHeight);
		constants.MieExtinction = glm::vec4(sky.MieScattering + sky.MieAbsorption, sky.MieAnisotropy);
		constants.OzoneAbsorption = glm::vec4(sky.OzoneAbsorption, sky.PlanetRadius);
		constants.GroundAlbedo = glm::vec4(sky.GroundAlbedo, sky.PlanetRadius + sky.AtmosphereHeight);
		constants.TowardsSun = glm::vec4(sky.TowardsSun, sky.CameraHeight);
		constants.SunIlluminance = glm::vec4(sky.SunIlluminance, 0.0f);
		constants.WarmthTint = glm::vec4(sky.WarmthTint, 0.0f);
		constants.NightLight = glm::vec4(sky.NightLight, 0.0f);
		constants.TransmittanceLut = tables.Transmittance.Slot;
		constants.MultipleScatteringLut = tables.MultipleScattering.Slot;
		constants.SkyViewLut = tables.SkyView.Slot;
		constants.SkyCube = tables.Cube.Slot;
		const SceneViewCloudCapture& clouds = tables.BuiltClouds;
		if (clouds.bClouds)
		{
			const SceneViewCloudLayerSettings& layer = clouds.Layer;
			constants.CloudLayer = 1;
			constants.CloudNoise = clouds.NoiseSlot;
			constants.CloudStrength = layer.LightStrength;
			constants.CloudShell = glm::vec4(layer.Altitude, layer.Thickness, layer.TileSize, layer.Thickness * CloudExtinction);
			constants.CloudShape = glm::vec4(layer.Coverage, layer.Type, layer.Softness, layer.Detail);
			constants.CloudWind = glm::vec4(layer.WindAxis, layer.Warp, layer.Rim);
			constants.CloudOrigin = glm::vec4(layer.Origin, layer.Brightness, layer.Ambient);
		}
		void* mapped = tables.ConstantsMemory.mapMemory(0, sizeof(SkyConstants));
		std::memcpy(mapped, &constants, sizeof(SkyConstants));
		tables.ConstantsMemory.unmapMemory();
	}

	void VulkanSkyAtmosphere::Record(SceneViewSkyTables& tables, vk::raii::CommandBuffer& cmd)
	{
		if (!tables.IsMade() || !tables.HasWork())
		{
			return;
		}
		tables.Pool.reset();

		if (tables.Pending.bAir)
		{
			SkyDispatch dispatch;
			dispatch.Width = TransmittanceWidth;
			dispatch.Height = TransmittanceHeight;
			Bindings bindings;
			bindings.Target = *tables.Transmittance.Levels[0];
			BeginWrite(cmd, tables.Transmittance);
			Dispatch(tables, cmd, "skyTransmittance", &dispatch, bindings, Groups(TransmittanceWidth), Groups(TransmittanceHeight), 1);
			EndWrite(cmd, tables.Transmittance);

			// A group a texel, of 64 directions
			dispatch.Width = MultipleScatteringSize;
			dispatch.Height = MultipleScatteringSize;
			bindings.Target = *tables.MultipleScattering.Levels[0];
			BeginWrite(cmd, tables.MultipleScattering);
			Dispatch(tables, cmd, "skyMultipleScattering", &dispatch, bindings, MultipleScatteringSize, MultipleScatteringSize, 1);
			EndWrite(cmd, tables.MultipleScattering);
			++tables.AirBuilds;
		}

		if (tables.Pending.bAir || tables.Pending.bSkyView)
		{
			SkyDispatch dispatch;
			dispatch.Width = SkyViewWidth;
			dispatch.Height = SkyViewHeight;
			Bindings bindings;
			bindings.Target = *tables.SkyView.Levels[0];
			BeginWrite(cmd, tables.SkyView);
			Dispatch(tables, cmd, "skySkyView", &dispatch, bindings, Groups(SkyViewWidth), Groups(SkyViewHeight), 1);
			EndWrite(cmd, tables.SkyView);
			++tables.SkyViewBuilds;
		}

		if (tables.Pending.bAir || tables.Pending.bSkyView || tables.bCubePending)
		{
			SkyDispatch dispatch;
			Bindings bindings;

			// The sky as a cube, the clouds in it while they light the world, and its mips
			BeginWrite(cmd, tables.Cube);
			dispatch.Width = CubeSize;
			dispatch.Height = CubeSize;
			bindings.Target = *tables.Cube.Levels[0];
			Dispatch(tables, cmd, "skyCube", &dispatch, bindings, Groups(CubeSize), Groups(CubeSize), 6);
			for (uint32_t level = 1; level < CubeLevels; ++level)
			{
				dispatch.Width = CubeSize >> level;
				dispatch.Height = dispatch.Width;
				bindings.Target = *tables.Cube.Levels[level];
				bindings.Previous = *tables.Cube.Levels[level - 1];
				Dispatch(tables, cmd, "skyCubeDownsample", &dispatch, bindings, Groups(dispatch.Width), Groups(dispatch.Width), 6);
			}
			EndWrite(cmd, tables.Cube);

			// Its diffuse light, from one of its mips
			BeginWrite(cmd, tables.Irradiance);
			SkyDispatch irradiance;
			irradiance.Width = IrradianceSize;
			irradiance.Height = IrradianceSize;
			irradiance.Source = tables.Cube.Slot;
			irradiance.SourceLevel = static_cast<float>(IrradianceSourceLevel);
			irradiance.SourceSize = static_cast<float>(CubeSize >> IrradianceSourceLevel);
			Bindings irradianceBindings;
			irradianceBindings.Target = *tables.Irradiance.Levels[0];
			Dispatch(tables, cmd, "skyIrradiance", &irradiance, irradianceBindings, Groups(IrradianceSize), Groups(IrradianceSize), 6);
			EndWrite(cmd, tables.Irradiance);

			// Its specular light, rougher with each mip
			BeginWrite(cmd, tables.Specular);
			for (uint32_t level = 0; level < SpecularLevels; ++level)
			{
				SkyDispatch specular;
				specular.Width = CubeSize >> level;
				specular.Height = specular.Width;
				specular.Source = tables.Cube.Slot;
				specular.Roughness = static_cast<float>(level) / static_cast<float>(SpecularLevels - 1);
				specular.SourceSize = static_cast<float>(CubeSize);
				Bindings specularBindings;
				specularBindings.Target = *tables.Specular.Levels[level];
				Dispatch(tables, cmd, "skySpecular", &specular, specularBindings, Groups(specular.Width), Groups(specular.Width), 6);
			}
			EndWrite(cmd, tables.Specular);
			++tables.CubeBuilds;
		}

		// The debug window's pictures, each frame it is open
		if (tables.bPreviews && tables.Previews.front().bHasSlot)
		{
			const std::array<uint32_t, 4> sources = { tables.Transmittance.Slot, tables.MultipleScattering.Slot, tables.SkyView.Slot, tables.Cube.Slot };
			for (uint32_t preview = 0; preview < tables.Previews.size(); ++preview)
			{
				SkyDispatch dispatch;
				dispatch.Width = PreviewSizes[preview].x;
				dispatch.Height = PreviewSizes[preview].y;
				dispatch.Source = sources[preview];
				dispatch.Mode = preview;
				// The multiple scattering is per unit of the sun's light, far dimmer than the sky
				dispatch.Exposure = tables.PreviewExposure * (preview == 1 ? 50.0f : 1.0f);
				Bindings bindings;
				bindings.Preview = *tables.Previews[preview].Levels[0];
				BeginWrite(cmd, tables.Previews[preview]);
				Dispatch(tables, cmd, "skyPreview", &dispatch, bindings, Groups(dispatch.Width), Groups(dispatch.Height), 1);
				EndWrite(cmd, tables.Previews[preview]);
			}
		}
		tables.Pending = {};
		tables.bCubePending = false;
	}

	void VulkanSkyAtmosphere::Release(SceneViewSkyTables& tables)
	{
		ReleaseImage(tables.Transmittance);
		ReleaseImage(tables.MultipleScattering);
		ReleaseImage(tables.SkyView);
		ReleaseImage(tables.Cube);
		ReleaseImage(tables.Irradiance);
		ReleaseImage(tables.Specular);
		for (SceneViewSkyTables::Image& preview : tables.Previews)
		{
			ReleaseImage(preview);
		}
		tables.Pool = nullptr;
		tables.Constants = nullptr;
		tables.ConstantsMemory = nullptr;
		tables.bBuilt = false;
		tables.Pending = {};
		tables.bCubePending = false;
		tables.BuiltClouds = {};
		tables.bPreviews = false;
	}

	void VulkanSkyAtmosphere::Invalidate(SceneViewSkyTables& tables)
	{
		tables.bBuilt = false;
	}

	SceneViewSkyTables::Image VulkanSkyAtmosphere::MakeImage(uint32_t width, uint32_t height, uint32_t levels, uint32_t layers, vk::Format format)
	{
		vk::raii::Device& device = Context.GetDevice();
		SceneViewSkyTables::Image image;
		image.Width = width;
		image.Height = height;
		image.LevelCount = levels;
		image.Layers = layers;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.flags = layers == 6 ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(width, height, 1);
		imageInfo.mipLevels = levels;
		imageInfo.arrayLayers = layers;
		imageInfo.format = format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		image.Handle = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements requirements = image.Handle.getMemoryRequirements();
		image.Memory = vk::raii::DeviceMemory(device,
			vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		image.Handle.bindMemory(*image.Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *image.Handle;
		viewInfo.viewType = layers == 6 ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, layers);
		image.Sampled = vk::raii::ImageView(device, viewInfo);

		// Storage views: a mip as an array of its layers (the tables' RWTexture2DArray), the
		// pictures' as a 2D image
		for (uint32_t level = 0; level < levels; ++level)
		{
			viewInfo.viewType = format == PreviewFormat ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray;
			viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, level, 1, 0, layers);
			image.Levels.emplace_back(device, viewInfo);
		}
		image.Slot = Bindless->AddTexture(*image.Sampled);
		image.bHasSlot = true;
		return image;
	}

	void VulkanSkyAtmosphere::ReleaseImage(SceneViewSkyTables::Image& image)
	{
		if (image.ImGuiTexture != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(image.ImGuiTexture);
		}
		if (image.bHasSlot)
		{
			Bindless->RemoveTexture(image.Slot);
		}
		image = SceneViewSkyTables::Image{};
	}

	void VulkanSkyAtmosphere::BeginWrite(vk::raii::CommandBuffer& cmd, const SceneViewSkyTables::Image& image)
	{
		// Written whole, so what was in it doesn't matter; after whatever read it before
		TransitionImageLayout(*cmd, *image.Handle, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, image.LevelCount, 0, image.Layers), vk::ImageLayout::eUndefined,
			vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlags{},
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	}

	void VulkanSkyAtmosphere::EndWrite(vk::raii::CommandBuffer& cmd, const SceneViewSkyTables::Image& image)
	{
		TransitionImageLayout(*cmd, *image.Handle, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, image.LevelCount, 0, image.Layers), vk::ImageLayout::eGeneral,
			vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite,
			vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
	}

	void VulkanSkyAtmosphere::Dispatch(SceneViewSkyTables& tables, vk::raii::CommandBuffer& cmd, const std::string& entryPoint, const void* constants,
		const Bindings& bindings, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::DescriptorSetLayout layout = *SetLayout;
		// Freed with the pool when the next ones are recorded
		const vk::DescriptorSet set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*tables.Pool, layout)).front().release();

		std::vector<vk::WriteDescriptorSet> writes;
		const vk::DescriptorBufferInfo constantsInfo(*tables.Constants, 0, sizeof(SkyConstants));
		const vk::DescriptorImageInfo target(nullptr, bindings.Target, vk::ImageLayout::eGeneral);
		const vk::DescriptorImageInfo previous(nullptr, bindings.Previous, vk::ImageLayout::eGeneral);
		const vk::DescriptorImageInfo preview(nullptr, bindings.Preview, vk::ImageLayout::eGeneral);
		writes.emplace_back(set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &constantsInfo);
		if (bindings.Target)
		{
			writes.emplace_back(set, 1, 0, 1, vk::DescriptorType::eStorageImage, &target);
		}
		if (bindings.Previous)
		{
			writes.emplace_back(set, 2, 0, 1, vk::DescriptorType::eStorageImage, &previous);
		}
		if (bindings.Preview)
		{
			writes.emplace_back(set, 3, 0, 1, vk::DescriptorType::eStorageImage, &preview);
		}
		device.updateDescriptorSets(writes, {});

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *Pipelines.at(entryPoint));
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *PipelineLayout, 0, { Bindless->GetSet(), set }, {});
		cmd.pushConstants<SkyDispatch>(*PipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, *static_cast<const SkyDispatch*>(constants));
		cmd.dispatch(groupsX, groupsY, groupsZ);

		// What this wrote, the next dispatch reads (a cube's mip the next is made from)
		const vk::MemoryBarrier written(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, written, {}, {});
	}

	std::vector<glm::vec4> VulkanSkyAtmosphere::ReadTable(SceneViewSkyTables& tables, ESkyTable table, uint32_t level)
	{
		SceneViewSkyTables::Image* images[] = { &tables.Transmittance, &tables.MultipleScattering, &tables.SkyView, &tables.Cube, &tables.Irradiance, &tables.Specular };
		SceneViewSkyTables::Image& image = *images[static_cast<size_t>(table)];
		if (!image.bHasSlot || level >= image.LevelCount)
		{
			return {};
		}
		Context.WaitIdle();
		const uint32_t width = std::max(image.Width >> level, 1u);
		const uint32_t height = std::max(image.Height >> level, 1u);
		const size_t texels = static_cast<size_t>(width) * height * image.Layers;
		vk::raii::Buffer readback{ nullptr };
		vk::raii::DeviceMemory readbackMemory{ nullptr };
		CreateBuffer(Context, texels * 8, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, readback,
			readbackMemory);

		vk::raii::CommandBuffer commands = BeginSingleTimeCommands(Context);
		const vk::ImageSubresourceRange levelRange(vk::ImageAspectFlagBits::eColor, level, 1, 0, image.Layers);
		TransitionImageLayout(*commands, *image.Handle, levelRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eTransferRead);
		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, image.Layers);
		copy.imageExtent = vk::Extent3D(width, height, 1);
		commands.copyImageToBuffer(*image.Handle, vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
		TransitionImageLayout(*commands, *image.Handle, levelRange, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
			vk::AccessFlagBits::eShaderRead);
		EndSingleTimeCommands(Context, std::move(commands));

		std::vector<uint16_t> halves(texels * 4);
		std::memcpy(halves.data(), readbackMemory.mapMemory(0, halves.size() * sizeof(uint16_t)), halves.size() * sizeof(uint16_t));
		readbackMemory.unmapMemory();
		std::vector<glm::vec4> values(texels);
		for (size_t texel = 0; texel < texels; ++texel)
		{
			values[texel] = glm::vec4(glm::unpackHalf1x16(halves[texel * 4]), glm::unpackHalf1x16(halves[texel * 4 + 1]), glm::unpackHalf1x16(halves[texel * 4 + 2]),
				glm::unpackHalf1x16(halves[texel * 4 + 3]));
		}
		return values;
	}

	ImTextureID VulkanSkyAtmosphere::GetPreviewTextureId(SceneViewSkyTables& tables, uint32_t preview)
	{
		if (preview >= tables.Previews.size() || !tables.Previews[preview].bHasSlot)
		{
			return ImTextureID_Invalid;
		}
		SceneViewSkyTables::Image& image = tables.Previews[preview];
		if (image.ImGuiTexture == VK_NULL_HANDLE)
		{
			image.ImGuiTexture = ImGui_ImplVulkan_AddTexture(*PreviewSampler, *image.Sampled, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		return reinterpret_cast<ImTextureID>(image.ImGuiTexture);
	}

	glm::uvec2 VulkanSkyAtmosphere::GetPreviewSize(const SceneViewSkyTables& tables, uint32_t preview) const
	{
		return preview < tables.Previews.size() && tables.Previews[preview].bHasSlot ? PreviewSizes[preview] : glm::uvec2(0);
	}
}

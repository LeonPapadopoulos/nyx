#include "NyxPCH.h"
#include "VulkanVolumetricClouds.h"

#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <array>
#include <span>

namespace Nyx
{
	namespace
	{
		constexpr const char* NoiseProgram = "VolumetricCloudNoise.slang";
		constexpr const char* WeatherProgram = "VolumetricCloudWeather.slang";
		constexpr const char* CloudsProgram = "VolumetricClouds.slang";

		// How far the camera may move, or the wind carry the clouds, in a frame, in kilometres, before
		// every pixel is marched again rather than carried: a jump, not a flight or a breeze
		constexpr float MostCarriedMove = 0.5f;

		// NoiseDispatch in VolumetricCloudNoise.slang and VolumetricCloudWeather.slang
		struct NoiseDispatch
		{
			uint32_t Size = 0;
			uint32_t Seed = 0;
			uint32_t Padding0 = 0;
			uint32_t Padding1 = 0;
		};
		static_assert(sizeof(NoiseDispatch) == 16, "NoiseDispatch must match VolumetricCloudNoise.slang");

		vk::raii::DeviceMemory AllocateFor(VulkanContext& context, const vk::raii::Image& image)
		{
			const vk::MemoryRequirements requirements = image.getMemoryRequirements();
			return vk::raii::DeviceMemory(context.GetDevice(),
				vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		}

		// Readable before it is first written, as a slot in the bindless array must be
		void MakeReadable(VulkanContext& context, vk::Image image, const vk::ImageSubresourceRange& range)
		{
			vk::raii::CommandBuffer commands = BeginSingleTimeCommands(context);
			TransitionImageLayout(*commands, image, range, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTopOfPipe, {},
				vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
			EndSingleTimeCommands(context, std::move(commands));
		}

		// A 2D or 3D image `size` texels a side with its mips, for the noises' compute shaders and
		// the passes that sample it
		void MakeNoise(VulkanContext& context, VulkanBindless& bindless, VolumetricCloudNoise& noise, uint32_t size, uint32_t levels, bool bVolume)
		{
			vk::raii::Device& device = context.GetDevice();
			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = bVolume ? vk::ImageType::e3D : vk::ImageType::e2D;
			imageInfo.format = VulkanVolumetricClouds::NoiseFormat;
			imageInfo.extent = vk::Extent3D(size, size, bVolume ? size : 1);
			imageInfo.mipLevels = levels;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			noise.Image = vk::raii::Image(device, imageInfo);
			noise.Memory = AllocateFor(context, noise.Image);
			noise.Image.bindMemory(*noise.Memory, 0);

			const vk::ImageViewType viewType = bVolume ? vk::ImageViewType::e3D : vk::ImageViewType::e2D;
			const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, levels, 0, 1);
			noise.View = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *noise.Image, viewType, VulkanVolumetricClouds::NoiseFormat, {}, whole));
			for (uint32_t level = 0; level < levels; ++level)
			{
				const vk::ImageSubresourceRange mip(vk::ImageAspectFlagBits::eColor, level, 1, 0, 1);
				noise.Levels.emplace_back(device, vk::ImageViewCreateInfo({}, *noise.Image, viewType, VulkanVolumetricClouds::NoiseFormat, {}, mip));
			}
			MakeReadable(context, *noise.Image, whole);
			noise.Slot = bindless.AddTexture(*noise.View);
		}

		void ReleaseNoise(VulkanBindless& bindless, VolumetricCloudNoise& noise)
		{
			if (noise.Slot != 0)
			{
				bindless.RemoveTexture(noise.Slot);
			}
			noise.Slot = 0;
			noise.Levels.clear();
			noise.View = nullptr;
			noise.Image = nullptr;
			noise.Memory = nullptr;
		}

		// An image of the half size the passes draw into and the next ones read
		void MakeTarget(VulkanContext& context, vk::Extent2D extent, vk::Format format, vk::raii::Image& image, vk::raii::DeviceMemory& memory, vk::raii::ImageView& view)
		{
			vk::raii::Device& device = context.GetDevice();
			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = vk::ImageType::e2D;
			imageInfo.format = format;
			imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			image = vk::raii::Image(device, imageInfo);
			memory = AllocateFor(context, image);
			image.bindMemory(*memory, 0);
			const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			view = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *image, vk::ImageViewType::e2D, format, {}, whole));
			MakeReadable(context, *image, whole);
		}

		void MakePicture(VulkanContext& context, VulkanBindless& bindless, vk::RenderPass renderPass, VolumetricCloudPicture& picture, vk::Extent2D extent)
		{
			MakeTarget(context, extent, VulkanVolumetricClouds::LightFormat, picture.Light, picture.LightMemory, picture.LightView);
			MakeTarget(context, extent, VulkanVolumetricClouds::DepthFormat, picture.Depth, picture.DepthMemory, picture.DepthView);
			const std::array<vk::ImageView, 2> attachments = { *picture.LightView, *picture.DepthView };
			picture.Framebuffer = vk::raii::Framebuffer(context.GetDevice(), vk::FramebufferCreateInfo({}, renderPass, attachments, extent.width, extent.height, 1));
			picture.LightSlot = bindless.AddTexture(*picture.LightView);
			picture.DepthSlot = bindless.AddTexture(*picture.DepthView);
		}

		void ReleasePicture(VulkanBindless& bindless, VolumetricCloudPicture& picture)
		{
			if (picture.LightSlot != 0)
			{
				bindless.RemoveTexture(picture.LightSlot);
				bindless.RemoveTexture(picture.DepthSlot);
			}
			picture = VolumetricCloudPicture{};
		}
	}

	VulkanVolumetricClouds::VulkanVolumetricClouds(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanVolumetricClouds::Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		Bindless = &bindless;
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);

		vk::raii::Device& device = Context.GetDevice();
		const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
			vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
		};
		NoiseSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
		const vk::DescriptorSetLayout setLayout = *NoiseSetLayout;
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(NoiseDispatch));
		NoisePipelineLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, setLayout, pushRange));

		CreateRenderPass();
		CreatePipelines();
	}

	void VulkanVolumetricClouds::Shutdown()
	{
		ShapePipeline = nullptr;
		DetailPipeline = nullptr;
		CurlPipeline = nullptr;
		VolumeDownsamplePipeline = nullptr;
		WeatherPipeline = nullptr;
		WeatherDownsamplePipeline = nullptr;
		MarchPipeline = nullptr;
		ResolvePipeline = nullptr;
		RenderPass = nullptr;
		NoisePipelineLayout = nullptr;
		NoiseSetLayout = nullptr;
		Bindless = nullptr;
	}

	void VulkanVolumetricClouds::CreateRenderPass()
	{
		// Two colour targets (the light, the depth) written, after the frame before's passes read
		// them, before this one's read them
		const std::array<vk::AttachmentReference, 2> colorReferences = {
			vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
			vk::AttachmentReference(1, vk::ImageLayout::eColorAttachmentOptimal),
		};
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpass.pColorAttachments = colorReferences.data();

		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eColorAttachmentWrite,
				vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead),
			vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead),
		};

		std::array<vk::AttachmentDescription, 2> attachments{};
		for (size_t index = 0; index < attachments.size(); ++index)
		{
			vk::AttachmentDescription& color = attachments[index];
			color.format = index == 0 ? LightFormat : DepthFormat;
			color.samples = vk::SampleCountFlagBits::e1;
			color.loadOp = vk::AttachmentLoadOp::eDontCare;
			color.storeOp = vk::AttachmentStoreOp::eStore;
			color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			color.initialLayout = vk::ImageLayout::eUndefined;
			color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		RenderPass = vk::raii::RenderPass(Context.GetDevice(), vk::RenderPassCreateInfo({}, attachments, subpass, dependencies));
	}

	void VulkanVolumetricClouds::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		const auto makeCompute = [&](const char* program, const char* entryPoint)
		{
			vk::raii::ShaderModule module = CreateShaderModule(Context, Programs.GetSpirv(program, entryPoint));
			vk::ComputePipelineCreateInfo info{};
			info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint);
			info.layout = *NoisePipelineLayout;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		ShapePipeline = makeCompute(NoiseProgram, "cloudShapeNoise");
		DetailPipeline = makeCompute(NoiseProgram, "cloudDetailNoise");
		CurlPipeline = makeCompute(NoiseProgram, "cloudCurlNoise");
		VolumeDownsamplePipeline = makeCompute(NoiseProgram, "cloudNoiseDownsample");
		WeatherPipeline = makeCompute(WeatherProgram, "cloudWeather");
		WeatherDownsamplePipeline = makeCompute(WeatherProgram, "cloudWeatherDownsample");

		// One triangle over the target, no vertices, no depth, no blending
		const auto makeGraphics = [&](const std::string& fragmentName)
		{
			const std::string vertexName = "volumetricCloudsVertex";
			vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv(CloudsProgram, vertexName));
			vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv(CloudsProgram, fragmentName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragmentName.c_str()),
			};
			vk::PipelineVertexInputStateCreateInfo vertexInput{};
			vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
			vk::PipelineViewportStateCreateInfo viewportState{};
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;
			vk::PipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.polygonMode = vk::PolygonMode::eFill;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = vk::CullModeFlagBits::eNone;
			vk::PipelineMultisampleStateCreateInfo multisampling{};
			multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
			vk::PipelineDepthStencilStateCreateInfo depthStencil{};
			std::array<vk::PipelineColorBlendAttachmentState, 2> blendAttachments{};
			blendAttachments[0].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			blendAttachments[1].colorWriteMask = blendAttachments[0].colorWriteMask;
			vk::PipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
			colorBlending.pAttachments = blendAttachments.data();
			const std::span<const vk::DynamicState> dynamicStates = std::span<const vk::DynamicState>(MeshDynamicStates).first(2);
			vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

			vk::GraphicsPipelineCreateInfo info{};
			info.stageCount = static_cast<uint32_t>(stages.size());
			info.pStages = stages.data();
			info.pVertexInputState = &vertexInput;
			info.pInputAssemblyState = &inputAssembly;
			info.pViewportState = &viewportState;
			info.pRasterizationState = &rasterizer;
			info.pMultisampleState = &multisampling;
			info.pDepthStencilState = &depthStencil;
			info.pColorBlendState = &colorBlending;
			info.pDynamicState = &dynamicState;
			info.layout = ScenePipelineLayout;
			info.renderPass = *RenderPass;
			return vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
		};
		MarchPipeline = makeGraphics("volumetricCloudsMarchFragment");
		ResolvePipeline = makeGraphics("volumetricCloudsResolveFragment");
	}

	void VulkanVolumetricClouds::Prepare(SceneViewVolumetricClouds& clouds, const VolumetricCloudsFrame& frame, const SceneViewVolumetricCloudsSettings& settings)
	{
		clouds.Settings = settings;
		if (!settings.bClouds)
		{
			Release(clouds);
			return;
		}
		if (clouds.Shape.Slot == 0)
		{
			MakeNoises(clouds);
		}
		if (clouds.NoiseSeed != settings.Seed)
		{
			clouds.NoiseSeed = settings.Seed;
			clouds.bNoisePending = true;
		}
		const vk::Extent2D extent = settings.bFullResolution ? frame.ViewExtent : vk::Extent2D((frame.ViewExtent.width + 1) / 2, (frame.ViewExtent.height + 1) / 2);
		if (clouds.Extent != extent)
		{
			ReleasePictures(clouds);
			MakePictures(clouds, extent);
		}

		// Carried from the frame before only if it drew them as they are now: the same look, the
		// same noises, the camera not jumped, the world's time not jumped either
		const VolumetricCloudsLook look{ settings, frame.TowardsSun };
		const glm::vec3 moved = glm::vec3((frame.Camera - clouds.Last.Camera) * 0.001);
		const glm::vec2 blown = settings.WindVelocity * static_cast<float>(frame.WorldTime - clouds.Last.WorldTime);
		// More marched while the picture moves fast under the camera: by its turn, and by its move as
		// seen at the nearest the clouds may be
		const float height = static_cast<float>(frame.Camera.y) * 0.001f;
		const float nearest = std::max({ settings.Bottom - height, height - settings.Top, 0.05f });
		clouds.Shift = clouds.bDrawn ? GetVolumetricCloudScreenShift(clouds.Last.SkyViewProjection, frame.SkyViewProjection, static_cast<float>(extent.height), moved, nearest) : 0.0f;
		clouds.BlockSize = GetVolumetricCloudBlockSize(settings.BlockSize, clouds.Shift / static_cast<float>(std::max(extent.height, 1u)));
		clouds.bMarchAll = clouds.BlockSize == 1 || !clouds.bDrawn || clouds.bNoisePending || IsVolumetricCloudsLookChanged(clouds.Marched, look) ||
			glm::length(moved) > MostCarriedMove || glm::length(blown) > MostCarriedMove;
		if (clouds.bMarchAll)
		{
			clouds.BlockSize = 1;
		}
		clouds.UpdateOffset = GetVolumetricCloudUpdateOffset(clouds.Frame, clouds.BlockSize);
		clouds.Written = clouds.Shown ^ 1u;
		if (clouds.bDrawn)
		{
			clouds.Motion = moved - glm::vec3(blown.x, 0.0f, blown.y);
			clouds.PreviousSkyViewProjection = clouds.Last.SkyViewProjection;
		}
		else
		{
			clouds.Motion = glm::vec3(0.0f);
			clouds.PreviousSkyViewProjection = frame.SkyViewProjection;
		}
		clouds.PendingLook = look;
		clouds.PendingFrame = frame;
	}

	void VulkanVolumetricClouds::Release(SceneViewVolumetricClouds& clouds)
	{
		if (clouds.Shape.Slot == 0 && clouds.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		ReleaseNoises(clouds);
		ReleasePictures(clouds);
	}

	void VulkanVolumetricClouds::Invalidate(SceneViewVolumetricClouds& clouds)
	{
		clouds.bNoisePending = clouds.Shape.Slot != 0;
		clouds.bDrawn = false;
	}

	void VulkanVolumetricClouds::MakeNoises(SceneViewVolumetricClouds& clouds)
	{
		MakeNoise(Context, *Bindless, clouds.Shape, VolumetricCloudShapeSize, VolumetricCloudShapeLevels, true);
		MakeNoise(Context, *Bindless, clouds.Detail, VolumetricCloudDetailSize, VolumetricCloudDetailLevels, true);
		MakeNoise(Context, *Bindless, clouds.Curl, VolumetricCloudDetailSize, VolumetricCloudDetailLevels, true);
		MakeNoise(Context, *Bindless, clouds.Weather, VolumetricCloudWeatherSize, VolumetricCloudWeatherLevels, false);
		const uint32_t sets = VolumetricCloudShapeLevels + 2 * VolumetricCloudDetailLevels + VolumetricCloudWeatherLevels;
		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageImage, sets * 2);
		clouds.Pool = vk::raii::DescriptorPool(Context.GetDevice(), vk::DescriptorPoolCreateInfo({}, sets, poolSize));
		clouds.bNoisePending = true;
	}

	void VulkanVolumetricClouds::MakePictures(SceneViewVolumetricClouds& clouds, vk::Extent2D extent)
	{
		MakePicture(Context, *Bindless, *RenderPass, clouds.March, extent);
		MakePicture(Context, *Bindless, *RenderPass, clouds.Pictures[0], extent);
		MakePicture(Context, *Bindless, *RenderPass, clouds.Pictures[1], extent);
		clouds.Extent = extent;
		clouds.bDrawn = false;
	}

	void VulkanVolumetricClouds::ReleaseNoises(SceneViewVolumetricClouds& clouds)
	{
		ReleaseNoise(*Bindless, clouds.Shape);
		ReleaseNoise(*Bindless, clouds.Detail);
		ReleaseNoise(*Bindless, clouds.Curl);
		ReleaseNoise(*Bindless, clouds.Weather);
		clouds.NoiseSeed = 0;
		clouds.bNoisePending = false;
		clouds.Pool = nullptr;
		clouds.bDrawn = false;
	}

	void VulkanVolumetricClouds::ReleasePictures(SceneViewVolumetricClouds& clouds)
	{
		if (clouds.Extent.width == 0)
		{
			return;
		}
		Context.WaitIdle();
		ReleasePicture(*Bindless, clouds.March);
		ReleasePicture(*Bindless, clouds.Pictures[0]);
		ReleasePicture(*Bindless, clouds.Pictures[1]);
		clouds.Extent = vk::Extent2D{ 0, 0 };
		clouds.bDrawn = false;
	}

	void VulkanVolumetricClouds::RecordNoises(SceneViewVolumetricClouds& clouds, vk::raii::CommandBuffer& cmd)
	{
		if (!clouds.bNoisePending || clouds.Shape.Slot == 0)
		{
			return;
		}
		vk::raii::Device& device = Context.GetDevice();
		clouds.Pool.reset();
		const vk::DescriptorSetLayout setLayout = *NoiseSetLayout;

		// Each noise written whole, after whatever read it before: its first mip by its own shader, the
		// others each halved from the one before
		const auto record = [&](VolumetricCloudNoise& noise, uint32_t size, uint32_t levels, bool bVolume, vk::Pipeline first, vk::Pipeline downsample)
		{
			const vk::ImageSubresourceRange whole(vk::ImageAspectFlagBits::eColor, 0, levels, 0, 1);
			TransitionImageLayout(*cmd, *noise.Image, whole, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlags{},
				vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			for (uint32_t level = 0; level < levels; ++level)
			{
				// Freed with the pool when the next ones are recorded
				const vk::DescriptorSet set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*clouds.Pool, setLayout)).front().release();
				const vk::DescriptorImageInfo target(nullptr, *noise.Levels[level], vk::ImageLayout::eGeneral);
				const vk::DescriptorImageInfo previous(nullptr, *noise.Levels[level == 0 ? 0 : level - 1], vk::ImageLayout::eGeneral);
				const std::array<vk::WriteDescriptorSet, 2> writes = {
					vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eStorageImage, &target),
					vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eStorageImage, &previous),
				};
				device.updateDescriptorSets(writes, {});

				NoiseDispatch dispatch;
				dispatch.Size = std::max(size >> level, 1u);
				dispatch.Seed = clouds.NoiseSeed;
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, level == 0 ? first : downsample);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *NoisePipelineLayout, 0, { set }, {});
				cmd.pushConstants<NoiseDispatch>(*NoisePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, dispatch);
				if (bVolume)
				{
					const uint32_t groups = (dispatch.Size + 3) / 4;
					cmd.dispatch(groups, groups, groups);
				}
				else
				{
					const uint32_t groups = (dispatch.Size + 7) / 8;
					cmd.dispatch(groups, groups, 1);
				}

				// What this wrote, the next mip is made from
				const vk::MemoryBarrier written(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, written, {}, {});
			}
			TransitionImageLayout(*cmd, *noise.Image, whole, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader,
				vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		};
		record(clouds.Shape, VolumetricCloudShapeSize, VolumetricCloudShapeLevels, true, *ShapePipeline, *VolumeDownsamplePipeline);
		record(clouds.Detail, VolumetricCloudDetailSize, VolumetricCloudDetailLevels, true, *DetailPipeline, *VolumeDownsamplePipeline);
		record(clouds.Curl, VolumetricCloudDetailSize, VolumetricCloudDetailLevels, true, *CurlPipeline, *VolumeDownsamplePipeline);
		record(clouds.Weather, VolumetricCloudWeatherSize, VolumetricCloudWeatherLevels, false, *WeatherPipeline, *WeatherDownsamplePipeline);
		clouds.bNoisePending = false;
		++clouds.NoiseBuilds;
	}

	void VulkanVolumetricClouds::DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, const VolumetricCloudPicture& target, vk::Extent2D extent, vk::Pipeline pipeline)
	{
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = *RenderPass;
		begin.framebuffer = *target.Framebuffer;
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		BindSceneSets(view, cmd);
		const ObjectConstants object{};
		cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}

	void VulkanVolumetricClouds::Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		SceneViewVolumetricClouds& clouds = view.VolumetricClouds;
		if (!clouds.IsDrawn())
		{
			return;
		}
		RecordNoises(clouds, cmd);

		// 1. The march: every pixel, or one of each square into a part of the target each way
		const uint32_t block = clouds.BlockSize;
		const vk::Extent2D marched((clouds.Extent.width + block - 1) / block, (clouds.Extent.height + block - 1) / block);
		DrawPass(view, cmd, clouds.March, marched, *MarchPipeline);
		// 2. This frame's picture, from the march and the frame before's
		DrawPass(view, cmd, clouds.Pictures[clouds.Written], clouds.Extent, *ResolvePipeline);

		// What the next frame carries from
		clouds.Shown = clouds.Written;
		clouds.bDrawn = true;
		clouds.Marched = clouds.PendingLook;
		clouds.Last = clouds.PendingFrame;
		++clouds.Frame;
		if (clouds.bMarchAll)
		{
			++clouds.FullMarches;
		}
	}
}

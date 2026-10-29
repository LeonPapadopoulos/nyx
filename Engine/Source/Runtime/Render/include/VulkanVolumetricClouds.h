#pragma once

#include "Lightning.h"
#include "VolumetricClouds.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;
	struct SceneViewInstance;

	// A noise of the volumetric clouds: a cube or a square that tiles, with its mips; a storage view of
	// each mip, one of all of it in the bindless array
	struct VolumetricCloudNoise
	{
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		std::vector<vk::raii::ImageView> Levels;
		uint32_t Slot = 0;
	};

	// A picture of the volumetric clouds at the size they are marched at: their light and what gets
	// through, the haze between taken in (RGBA16F); and where along each ray they are, in kilometres,
	// and what of the sun gets through them (RGBA16F: their light's mean depth, where they start,
	// where they all but end, and what gets through them without the haze, for the sun's disk);
	// both in the bindless array, and the framebuffer that writes both
	struct VolumetricCloudPicture
	{
		vk::raii::Image Light{ nullptr };
		vk::raii::DeviceMemory LightMemory{ nullptr };
		vk::raii::ImageView LightView{ nullptr };
		vk::raii::Image Depth{ nullptr };
		vk::raii::DeviceMemory DepthMemory{ nullptr };
		vk::raii::ImageView DepthView{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
		uint32_t LightSlot = 0;
		uint32_t DepthSlot = 0;
	};

	// What the clouds of a view need of it each frame to be carried from the frame before: its
	// rotation and projection without its place, the camera's place (metres), the world's time
	// (seconds) and the way to the sun
	struct VolumetricCloudsFrame
	{
		vk::Extent2D ViewExtent{ 0, 0 };
		glm::mat4 SkyViewProjection{ 1.0f };
		glm::dvec3 Camera{ 0.0 };
		double WorldTime = 0.0;
		glm::vec3 TowardsSun{ 0.0f, 1.0f, 0.0f };
	};

	// A view's volumetric clouds on the GPU (VulkanVolumetricClouds): their noises, made from their
	// seed; the picture this frame marches into, and two pictures of the clouds, one written each
	// frame from the other. Made at the first frame they are drawn (VulkanVolumetricClouds::Prepare()),
	// the noises again when the seed changes, the pictures when the view's size does; released when
	// they go off, the view no longer draws the air's sky, and with the view.
	struct SceneViewVolumetricClouds
	{
		VolumetricCloudNoise Shape;
		VolumetricCloudNoise Detail;
		VolumetricCloudNoise Curl;
		VolumetricCloudNoise Weather;
		// The seed they were made from, and whether this frame makes them again
		uint32_t NoiseSeed = 0;
		bool bNoisePending = false;
		// A set a dispatch, freed when the next ones are recorded
		vk::raii::DescriptorPool Pool{ nullptr };

		// The size they are marched at: half the view's each way, or its own
		vk::Extent2D Extent{ 0, 0 };
		VolumetricCloudPicture March;
		VolumetricCloudPicture Pictures[2];

		// This frame: its settings (the world's first VolumetricCloudsComponent's); whether it marches
		// every pixel, else one of each 4 x 4, which; the picture it writes (the other is the last
		// frame drawn's); how far the camera moved less the wind since the frame before
		// (kilometres), and its rotation and projection then
		SceneViewVolumetricCloudsSettings Settings;
		bool bMarchAll = true;
		// The squares of which one pixel is marched this frame, pixels a side (1 every pixel), which
		// pixel of each, and how far the picture moved under the camera since the frame before, in
		// its pixels
		uint32_t BlockSize = 1;
		glm::uvec2 UpdateOffset{ 0 };
		float Shift = 0.0f;
		uint32_t Written = 0;
		uint32_t Shown = 1;
		glm::vec3 Motion{ 0.0f };
		glm::mat4 PreviousSkyViewProjection{ 1.0f };

		// Whether a picture of the clouds was drawn, and what it was marched with, for the next frame
		bool bDrawn = false;
		VolumetricCloudsLook Marched;
		uint32_t Frame = 0;
		VolumetricCloudsFrame Last;
		// This frame's, kept by Draw() when it is drawn
		VolumetricCloudsLook PendingLook;
		VolumetricCloudsFrame PendingFrame;

		// How many times the noises were made, and frames marched every pixel, for tests
		uint32_t NoiseBuilds = 0;
		uint32_t FullMarches = 0;

		// The lightning's light in them (Docs/Design/lightning.md): a picture at the size they are
		// marched at, written whole each frame a bolt lights them, never carried; this frame's lines of
		// light (PickLightningCloudLights()) and the rectangle of the picture they can show on (the
		// rest cleared)
		vk::raii::Image LightningImage{ nullptr };
		vk::raii::DeviceMemory LightningMemory{ nullptr };
		vk::raii::ImageView LightningView{ nullptr };
		vk::raii::Framebuffer LightningFramebuffer{ nullptr };
		uint32_t LightningSlot = 0;
		std::vector<LightningCloudLight> Lightning;
		LightningRect LightningArea;

		bool IsDrawn() const { return Settings.bClouds && Extent.width > 0 && Shape.Slot != 0; }
	};

	// The volumetric clouds (Docs/Design/atmosphere.md), per view, before its scene pass and after its
	// sky's tables, which they read:
	//   1. their noises, when made or their seed changed: VolumetricCloudNoise.slang's and
	//      VolumetricCloudWeather.slang's compute shaders, each with its mips;
	//   2. the march (VolumetricClouds.slang), at half the view's size: every pixel, or with Temporal
	//      one of each 4 x 4 a frame;
	//   3. the resolve: this frame's picture, from the march and the frame before's;
	//   4. in frames a bolt lights them (DrawLightning()), its light in them, every pixel within its
	//      reach, into a picture of its own the readers add (nyx::volumetricCloudsAt()), never carried
	//      from frame to frame, so a flash leaves nothing behind.
	// The passes are one triangle with the scene pipelines' layout, reading the view's set. The sky
	// pass draws the sky behind them, the surfaces beyond them are covered, the light shafts are held
	// back by what of the light gets through (nyx.clouds).
	class VulkanVolumetricClouds
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		static constexpr vk::Format NoiseFormat = vk::Format::eR8G8B8A8Unorm;
		static constexpr vk::Format LightFormat = vk::Format::eR16G16B16A16Sfloat;
		static constexpr vk::Format DepthFormat = vk::Format::eR16G16B16A16Sfloat;

		// Both must outlive it
		VulkanVolumetricClouds(VulkanContext& context, ShaderPrograms& programs);

		// Its render pass and pipelines; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when its shaders changed
		void CreatePipelines();

		// While no frame is in flight and before this frame's passes are recorded: the view's noises
		// and pictures for its size, made when new, the noises again for a new seed; what this frame
		// marches and carries. Released when its settings are off.
		void Prepare(SceneViewVolumetricClouds& clouds, const VolumetricCloudsFrame& frame, const SceneViewVolumetricCloudsSettings& settings);
		void Release(SceneViewVolumetricClouds& clouds);
		// Its noises must be made again and every pixel marched (the shaders changed)
		void Invalidate(SceneViewVolumetricClouds& clouds);

		// Outside a render pass, after the view's sky tables and before its scene pass, with its set
		// written: its noises if pending, the march and the resolve, the pictures left in
		// ShaderReadOnlyOptimal layout for the scene pass and the light shafts. Nothing for clouds not
		// drawn.
		void Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);
		// After Draw(), in a frame the view's lines of light are set (SceneViewVolumetricClouds::
		// Lightning): their light in the clouds into its picture, cleared, within their rectangle; left
		// in ShaderReadOnlyOptimal layout. Nothing without them.
		void DrawLightning(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPass();
		void MakeNoises(SceneViewVolumetricClouds& clouds);
		void MakePictures(SceneViewVolumetricClouds& clouds, vk::Extent2D extent);
		void ReleaseNoises(SceneViewVolumetricClouds& clouds);
		void ReleasePictures(SceneViewVolumetricClouds& clouds);
		void RecordNoises(SceneViewVolumetricClouds& clouds, vk::raii::CommandBuffer& cmd);
		void DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, const VolumetricCloudPicture& target, vk::Extent2D extent, vk::Pipeline pipeline);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// The noises' compute shaders: the mip written and the one before, as storage images
		vk::raii::DescriptorSetLayout NoiseSetLayout{ nullptr };
		vk::raii::PipelineLayout NoisePipelineLayout{ nullptr };
		vk::raii::Pipeline ShapePipeline{ nullptr };
		vk::raii::Pipeline DetailPipeline{ nullptr };
		vk::raii::Pipeline CurlPipeline{ nullptr };
		vk::raii::Pipeline VolumeDownsamplePipeline{ nullptr };
		vk::raii::Pipeline WeatherPipeline{ nullptr };
		vk::raii::Pipeline WeatherDownsamplePipeline{ nullptr };
		// Writes a picture whole
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Pipeline MarchPipeline{ nullptr };
		vk::raii::Pipeline ResolvePipeline{ nullptr };
		// The lightning's light in them: one colour target, cleared
		vk::raii::RenderPass LightningPass{ nullptr };
		vk::raii::Pipeline LightningPipeline{ nullptr };
	};
}

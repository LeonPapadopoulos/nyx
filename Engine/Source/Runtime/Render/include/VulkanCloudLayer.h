#pragma once

#include "CloudLayer.h"

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

	// A view's cloud layer on the GPU (VulkanCloudLayer): the noise it is made from, made from its
	// seed, and a target at the view's size its pass draws into. Made at the first frame it is drawn
	// (VulkanCloudLayer::Prepare()), the noise again when the seed changes, the target when the
	// view's size does; released when the layer goes off, the view no longer draws the air's sky,
	// and with the view.
	struct SceneViewCloudLayer
	{
		// The noise, a square that tiles, with its mips: a storage view of each, one of all of it in
		// the bindless array
		vk::raii::Image Noise{ nullptr };
		vk::raii::DeviceMemory NoiseMemory{ nullptr };
		vk::raii::ImageView NoiseView{ nullptr };
		std::vector<vk::raii::ImageView> NoiseLevels;
		uint32_t NoiseSlot = 0;
		// The seed it was made from, and whether this frame makes it again
		uint32_t NoiseSeed = 0;
		bool bNoisePending = false;
		// A set a dispatch, freed when the next ones are recorded
		vk::raii::DescriptorPool Pool{ nullptr };

		// The clouds' light along each pixel's ray and what gets through them (a), RGBA16F
		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Image Target{ nullptr };
		vk::raii::DeviceMemory TargetMemory{ nullptr };
		vk::raii::ImageView TargetView{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
		uint32_t TargetSlot = 0;

		// Its settings this frame (the world's first CloudLayerComponent's)
		SceneViewCloudLayerSettings Settings;
		// How many times its noise was made, for tests
		uint32_t NoiseBuilds = 0;

		bool IsDrawn() const { return Settings.bClouds && Extent.width > 0 && NoiseSlot != 0; }
	};

	// The high cloud layer (Docs/Design/atmosphere.md), per view, before its scene pass and after its
	// sky's tables, which it reads:
	//   1. its noise, when it is made or its seed changed: CloudNoise.slang's compute shaders, the
	//      square and its mips;
	//   2. the clouds along each pixel's ray, at the view's size: CloudLayer.slang, one triangle with
	//      the scene pipelines' layout, reading the view's set.
	// The sky pass draws the sky behind them, the surfaces beyond the layer's shell are covered by
	// them, the light shafts are held back by what of the light gets through them (nyx.clouds).
	class VulkanCloudLayer
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		static constexpr vk::Format Format = vk::Format::eR16G16B16A16Sfloat;

		// Both must outlive it
		VulkanCloudLayer(VulkanContext& context, ShaderPrograms& programs);

		// Its render pass and pipelines; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when CloudLayer.slang or CloudNoise.slang changed
		void CreatePipelines();

		// While no frame is in flight and before this frame's passes are recorded: the view's noise
		// and target for its size (`viewExtent`), made when new, the noise again for a new seed;
		// released when its settings are off
		void Prepare(SceneViewCloudLayer& layer, vk::Extent2D viewExtent, const SceneViewCloudLayerSettings& settings);
		void Release(SceneViewCloudLayer& layer);
		// Its noise must be made again (the shaders changed)
		void Invalidate(SceneViewCloudLayer& layer);

		// Outside a render pass, before the view's sky tables (whose cube may draw the clouds): its
		// noise, if it is new or its seed changed; nothing otherwise
		void RecordNoise(SceneViewCloudLayer& layer, vk::raii::CommandBuffer& cmd);
		// Outside a render pass, after the view's sky tables and before its scene pass, with its set
		// written: its noise if still pending, then its clouds, the target left in ShaderReadOnlyOptimal
		// layout for the scene pass and the light shafts. Nothing for a layer not drawn.
		void Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPass();
		void MakeNoise(SceneViewCloudLayer& layer);
		void MakeTarget(SceneViewCloudLayer& layer, vk::Extent2D extent);
		void ReleaseNoise(SceneViewCloudLayer& layer);
		void ReleaseTarget(SceneViewCloudLayer& layer);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// The noise's compute shaders: the mip written and the one before, as storage images
		vk::raii::DescriptorSetLayout NoiseSetLayout{ nullptr };
		vk::raii::PipelineLayout NoisePipelineLayout{ nullptr };
		vk::raii::Pipeline NoisePipeline{ nullptr };
		vk::raii::Pipeline DownsamplePipeline{ nullptr };
		// Writes the target whole
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Pipeline CloudPipeline{ nullptr };
	};
}

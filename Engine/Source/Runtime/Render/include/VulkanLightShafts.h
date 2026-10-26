#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <functional>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;
	struct SceneViewInstance;

	// A half-size target of the view's light shafts: drawn into, and read from the bindless array
	struct LightShaftsTarget
	{
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
		uint32_t Slot = 0;
	};

	// A view's light shafts on the GPU: two targets at half its size, the light they gather from
	// and what they gathered (the second holds the blur between). Made for its size at the first
	// frame they are on (VulkanLightShafts::Prepare()), released when they go off and with the view;
	// kept while the sun is off the picture, only not drawn.
	struct SceneViewLightShafts
	{
		vk::Extent2D Extent{ 0, 0 };
		LightShaftsTarget Gathered;
		LightShaftsTarget Blurred;
		// Its settings this frame (the world's environment's, under its sun); drawn while IsDrawn()
		SceneViewLightShaftsSettings Settings;
	};

	// Light shafts (Docs/Design/atmosphere.md): the sun's light streaming through gaps in what
	// stands before it, as a blur along lines out from the sun's place on the picture (the
	// crepuscular rays of a screen-space radial blur). After the view's particles, from its HDR
	// scene and its depth, LightShafts.slang:
	//   1. the light that may stream, halved: the scene's brightness, exposed and held below a
	//      most, where the depth says nothing nearer than the threshold stands before the sky,
	//      and the more of it the nearer to the sun;
	//   2. each pixel's light gathered along its line towards the sun, a fixed number of samples,
	//      each weighed by the decay, the line's start turned by a 4 x 4 noise;
	//   3. evened out along the line, over a sample's step, so the noise goes.
	// The tonemap adds them, filtered up, times their colour (ViewConstants.LightShafts), as it
	// adds the bloom. Each pass is one triangle with the scene pipelines' layout, its source in the
	// draw's push constants.
	class VulkanLightShafts
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// One channel: the shafts' brightness; their colour is the sun's, in the view's set
		static constexpr vk::Format Format = vk::Format::eR16Sfloat;

		// Both must outlive it
		VulkanLightShafts(VulkanContext& context, ShaderPrograms& programs);

		// Its render pass; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when LightShafts.slang changed
		void CreatePipelines();

		// While no frame is in flight and before this frame's draws are recorded: the view's targets
		// for its size (`viewExtent`), made again when it changed; released when the shafts are off
		void Prepare(SceneViewLightShafts& shafts, vk::Extent2D viewExtent, const SceneViewLightShaftsSettings& settings);
		void Release(SceneViewLightShafts& shafts);

		// After the view's particles, outside a render pass, its HDR scene in ShaderReadOnlyOptimal
		// layout: its shafts gathered, for its tonemap; its depth back in
		// DepthStencilAttachmentOptimal. Nothing for a view whose shafts are off or not drawn.
		void Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPass();
		void CreateTarget(LightShaftsTarget& target, vk::Extent2D extent);
		void ReleaseTarget(LightShaftsTarget& target);
		void DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, const LightShaftsTarget& target, vk::Extent2D extent, vk::Pipeline pipeline, uint32_t sourceSlot);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// Writes a target whole
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Pipeline PrefilterPipeline{ nullptr };
		vk::raii::Pipeline BlurPipeline{ nullptr };
		vk::raii::Pipeline SmoothPipeline{ nullptr };
	};
}

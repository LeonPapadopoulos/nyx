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
	class VulkanViewportTarget;
	struct SceneViewInstance;

	// A half-size target of the view's ambient occlusion: drawn into, and read from the bindless array
	struct AmbientOcclusionTarget
	{
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
		uint32_t Slot = 0;
	};

	// A view's ambient occlusion on the GPU: its depth in metres and its occlusion at half its size,
	// a target the blur goes through, and the pass that adds the occluded sky's light to its scene.
	// Made for its size at the first frame it is on (VulkanAmbientOcclusion::Prepare()), again when
	// the view's targets are made again, released when it goes off and with the view.
	struct SceneViewAmbientOcclusion
	{
		vk::Extent2D Extent{ 0, 0 };
		AmbientOcclusionTarget Depth;
		AmbientOcclusionTarget Occlusion;
		AmbientOcclusionTarget Blurred;
		// Into the view's HDR scene
		vk::raii::Framebuffer Composite{ nullptr };
		// Its settings this frame (the world's environment's); none drawn when off
		SceneViewAmbientOcclusionSettings Settings;
	};

	// Ambient occlusion (Docs/Design/atmosphere.md): how much of the sky's light reaches each pixel,
	// found from the scene's depth, applied to the sky's light alone, never to the sun's. The scene
	// pass writes the sky's light apart, into the view's ambient target (nyx.surface, while
	// ViewConstants.AmbientOcclusion is 1); after it, AmbientOcclusion.slang:
	//   1. the depth halved, as metres along the view;
	//   2. the occlusion at half size, ground truth ambient occlusion (a few slices, the horizons
	//      on both sides of each, the visible arc integrated), turned by a 4 x 4 noise;
	//   3. blurred along x and y, 4 texels wide, so the noise evens out, not across edges;
	//   4. the sky's light, times the occlusion filtered up by depth (a bilateral upsample), added
	//      to the scene.
	// Each pass is one triangle with the scene pipelines' layout, its sources in the draw's push
	// constants.
	class VulkanAmbientOcclusion
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		static constexpr vk::Format DepthFormat = vk::Format::eR32Sfloat;
		static constexpr vk::Format OcclusionFormat = vk::Format::eR8Unorm;

		// Both must outlive it
		VulkanAmbientOcclusion(VulkanContext& context, ShaderPrograms& programs);

		// Its render passes; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when AmbientOcclusion.slang changed
		void CreatePipelines();

		// While no frame is in flight and before this frame's draws are recorded: the view's targets
		// for its size, made again when it changed or `bTargetsRemade` (the view's own targets were
		// made again); released when it is off
		void Prepare(SceneViewAmbientOcclusion& occlusion, const VulkanViewportTarget& target, const SceneViewAmbientOcclusionSettings& settings, bool bTargetsRemade);
		void Release(SceneViewAmbientOcclusion& occlusion);

		// After the view's scene pass, outside a render pass: its occlusion, and the occluded sky's
		// light added to its HDR scene, which ends in ShaderReadOnlyOptimal layout; its depth back in
		// DepthStencilAttachmentOptimal. Nothing for a view whose occlusion is off.
		void Draw(SceneViewInstance& view, uint32_t sceneDepthSlot, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPasses();
		void CreateTarget(AmbientOcclusionTarget& target, vk::Format format, vk::RenderPass renderPass, vk::Extent2D extent);
		void ReleaseTarget(AmbientOcclusionTarget& target);
		void DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, vk::RenderPass renderPass, vk::Framebuffer framebuffer, vk::Extent2D extent, vk::Pipeline pipeline,
			uint32_t firstSlot, uint32_t secondSlot, vk::Extent2D sourceExtent, const glm::vec2& values);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// Write a target whole: the depth's and the occlusion's; adds into the HDR scene
		vk::raii::RenderPass DepthPass{ nullptr };
		vk::raii::RenderPass OcclusionPass{ nullptr };
		vk::raii::RenderPass CompositePass{ nullptr };
		vk::raii::Pipeline DepthPipeline{ nullptr };
		vk::raii::Pipeline OcclusionPipeline{ nullptr };
		vk::raii::Pipeline BlurPipeline{ nullptr };
		vk::raii::Pipeline CompositePipeline{ nullptr };
	};
}

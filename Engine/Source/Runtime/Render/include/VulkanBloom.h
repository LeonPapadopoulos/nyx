#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>

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

	// A view's bloom on the GPU: one image whose mips are the chain's levels, the first half the
	// view's size, each a target to draw into and a texture in the bindless array to read from.
	// Made for the view's size and levels at the first frame that blooms (VulkanBloom::Prepare()),
	// and again when they change. Released with the view.
	struct SceneViewBloom
	{
		vk::Extent2D Extent{ 0, 0 };
		uint32_t Levels = 0;
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		std::vector<vk::raii::ImageView> Views;
		std::vector<vk::raii::Framebuffer> Framebuffers;
		std::vector<uint32_t> Slots;
		// Its settings this frame (the world's environment's); none drawn when off
		SceneViewBloomSettings Settings;
	};

	// Bloom (Docs/Design/atmosphere.md): the light of what is brighter than a threshold spread
	// round it, as a lens and an eye spread it, so emissive surfaces and decals glow. Between the
	// view's scene pass and its tonemap, from the HDR scene:
	//   1. the scene, exposed, its bright parts kept (a soft threshold with a knee), halved into the
	//      chain's first level, each 2 x 2 group weighed down by its brightness, so a single bright
	//      pixel doesn't flicker as it moves;
	//   2. each level halved into the next (13 taps, Jimenez's), down to the last;
	//   3. from the last up, each level filtered up (a cubic B-spline, smooth and a little blurred)
	//      and mixed into the one above, by the radius: the further the light spreads.
	// The tonemap adds the first level, by the intensity (ViewConstants.BloomTexture). The passes
	// are each one triangle with Bloom.slang, the scene pipelines' layout, their source and values
	// in the draw's push constants.
	class VulkanBloom
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// Both must outlive it
		VulkanBloom(VulkanContext& context, ShaderPrograms& programs);

		// The chain's format (B10G11R11, or RGBA16 where it can't be drawn into and blended), its
		// render passes; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when Bloom.slang changed
		void CreatePipelines();

		vk::Format GetFormat() const { return Format; }

		// While no frame is in flight and before this frame's draws are recorded: the view's chain
		// for its size and settings, made again when they changed; released when it doesn't bloom
		void Prepare(SceneViewBloom& bloom, vk::Extent2D viewExtent, const SceneViewBloomSettings& settings);
		void Release(SceneViewBloom& bloom);

		// The view's chain, from its HDR scene (in `hdrSlot`, in ShaderReadOnlyOptimal layout),
		// outside a render pass; its first level ends in ShaderReadOnlyOptimal layout for the
		// tonemap. Nothing for a view whose bloom is off.
		void Draw(SceneViewInstance& view, uint32_t hdrSlot, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPasses();
		// One pass of the chain into `level`, from `sourceSlot`, `sourceSize` texels big
		void DrawPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd, vk::RenderPass renderPass, uint32_t level, vk::Pipeline pipeline, uint32_t sourceSlot,
			const glm::uvec2& sourceSize, const glm::vec2& values);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		vk::Format Format = vk::Format::eUndefined;
		// Writes a level whole; blends into a level drawn before. Compatible, so one set of
		// pipelines serves both.
		vk::raii::RenderPass DownPass{ nullptr };
		vk::raii::RenderPass UpPass{ nullptr };
		vk::raii::Pipeline PrefilterPipeline{ nullptr };
		vk::raii::Pipeline DownsamplePipeline{ nullptr };
		vk::raii::Pipeline UpsamplePipeline{ nullptr };
	};
}

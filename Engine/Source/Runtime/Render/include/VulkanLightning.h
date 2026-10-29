#pragma once

#include "Lightning.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;
	class VulkanViewportTarget;
	struct SceneViewInstance;

	// One draw of a view's lightning: a bolt's strips, VertexCount vertices from FirstVertex on in
	// this frame's lightning stream, and how they look
	struct SceneViewLightningDraw
	{
		uint32_t FirstVertex = 0;
		uint32_t VertexCount = 0;
		LightningLook Look;
	};

	// A view's lightning: what it draws this frame; its bolts' own picture at its size (RGBA16F, in
	// the bindless array), into which the strips are drawn, each pixel the brightest of what covers
	// it, and the framebuffers that write it and add it to the HDR scene; made the first frame it
	// draws a bolt, again for a new size, released with the view
	struct SceneViewLightning
	{
		std::vector<SceneViewLightningDraw> Draws;
		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		uint32_t Slot = 0;
		vk::raii::Framebuffer Strips{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
	};

	// Lightning's bolts on the GPU (Docs/Design/lightning.md): each frame's strips of every view in
	// one vertex stream, and per view, after its ambient occlusion and before its particles, two
	// passes with no depth buffer (Lightning.slang tests each pixel against the scene's depth, which
	// it reads, as the particles do):
	//   1. the strips into the view's own lightning picture, cleared, each pixel keeping the
	//      brightest of the strips over it (blended by their maximum), so where a jagged path's quads
	//      overlap at its kinks, or two paths cross, nothing is counted twice;
	//   2. that picture added to the HDR scene, one triangle over it.
	// Both with the scene pipelines' layout; each bolt's look in its draw's push constants. With
	// multisample anti-aliasing they come after the scene pass's resolve, one sample a pixel, as the
	// particles' pass does.
	class VulkanLightning
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// Both must outlive it
		VulkanLightning(VulkanContext& context, ShaderPrograms& programs);

		// Its render passes and pipelines; drawn with the scene pipelines' layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when Lightning.slang changed
		void CreatePipelines();

		// While no frame is in flight, after its draws were culled: the view's picture and framebuffers
		// for its size, made when it first draws a bolt, again when its size changed or
		// `bTargetsRemade` (the view's own targets were made again)
		void Prepare(SceneViewLightning& lightning, const VulkanViewportTarget& target, bool bTargetsRemade);
		void Release(SceneViewLightning& lightning);

		// This frame's strips of every view, into the stream, made bigger when they don't fit; while
		// no frame reads it (one frame in flight)
		void Upload(std::span<const LightningVertex> vertices);
		// After the last frame's fence: the streams it outgrew
		void ReleaseRetired();

		// After the view's ambient occlusion, outside a render pass: its draws into its picture, then
		// that added to its HDR scene, which stays in ShaderReadOnlyOptimal layout; its depth read,
		// and back in DepthStencilAttachmentOptimal. Nothing for a view without draws.
		void Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

	private:
		void ReleasePicture(SceneViewLightning& lightning);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// The strips into the view's picture, and that picture over its HDR scene
		vk::raii::RenderPass StripsPass{ nullptr };
		vk::raii::RenderPass Pass{ nullptr };
		vk::raii::Pipeline Pipeline{ nullptr };
		vk::raii::Pipeline CompositePipeline{ nullptr };

		vk::raii::Buffer Stream{ nullptr };
		vk::raii::DeviceMemory StreamMemory{ nullptr };
		size_t StreamCapacity = 0;
		std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> RetiredStreams;
	};
}

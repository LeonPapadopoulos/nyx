#pragma once

#include "ShadowCascades.h"

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <functional>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;
	struct SceneViewInstance;

	// The primary light's shadow maps on the GPU (Docs/Design/shadows.md): one depth texture array, a
	// layer a cascade, which every view's cascades are drawn into in turn, each view's just before its
	// scene pass reads them, so one map serves every view. A view draws into as much of each layer as
	// its light asks for (its resolution, from the layer's corner); the map is as big as the largest
	// any view asks for this frame. In the bindless array, so the surface shaders find it
	// (ViewConstants.ShadowMap, nyx.shadows).
	//
	// What casts is drawn by the renderer between BeginCascade() and EndCascade(), with the casters'
	// pipeline (Shadow.slang's shadowVertex): depth only, reversed as the scene's, the faces the
	// material draws (SetMeshFaces()), so a two-sided material casts from both sides.
	class VulkanShadows
	{
	public:
		// Binds the scene pipelines' two sets, the global one and the view's, for a draw into the view
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		static constexpr vk::Format DepthFormat = vk::Format::eD32Sfloat;
		static constexpr uint32_t MinResolution = 256;
		static constexpr uint32_t MaxResolution = 4096;
		// The map's size until a view asks for another
		static constexpr uint32_t InitialSize = 1024;

		// A cascade's resolution as the map has it: the power of two nearest, 256 to 4096
		static uint32_t ClampResolution(int32_t resolution);

		// Both must outlive it
		VulkanShadows(VulkanContext& context, ShaderPrograms& programs);

		// The map, in a slot of `bindless`, and its render pass; drawn with the scene pipelines'
		// layout. All must outlive it.
		void Initialize(VulkanBindless& bindless, vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once the first view is made, and again when Shadow.slang changed: the casters' pipeline,
		// and the debug view's for the views' picture pass, which every view's is like
		void CreatePipelines(vk::RenderPass picturePass);

		// While no frame is in flight and before this frame's draws are recorded: the map `size`
		// texels a side, made again (and put into a new slot) when that changed
		void EnsureSize(uint32_t size);
		uint32_t GetSize() const { return Size; }
		// The map in the bindless array, as a 2D texture array
		uint32_t GetSlot() const { return Slot; }

		// Starts drawing a cascade into its layer, `resolution` texels a side from its corner, the
		// depth cleared to nothing (0, reversed), with the casters' pipeline bound; then EndCascade()
		void BeginCascade(vk::raii::CommandBuffer& cmd, uint32_t cascade, uint32_t resolution);
		void EndCascade(vk::raii::CommandBuffer& cmd);

		// Inside the view's picture pass: each of its cascades' layer in a square along the picture's
		// bottom left, white nearest the light, framed in its cascade's tint
		void DrawDebug(SceneViewInstance& view, uint32_t cascadeCount, vk::raii::CommandBuffer& cmd);

	private:
		void CreateRenderPass();
		void CreateMap(uint32_t size);
		void DestroyMap();

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Pipeline CasterPipeline{ nullptr };
		vk::raii::Pipeline DebugPipeline{ nullptr };

		uint32_t Size = 0;
		uint32_t Slot = 0;
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		// The whole array, sampled; a layer each, drawn into
		vk::raii::ImageView ArrayView{ nullptr };
		std::array<vk::raii::ImageView, MaxShadowCascades> LayerViews{ nullptr, nullptr, nullptr, nullptr };
		std::array<vk::raii::Framebuffer, MaxShadowCascades> Framebuffers{ nullptr, nullptr, nullptr, nullptr };
	};
}

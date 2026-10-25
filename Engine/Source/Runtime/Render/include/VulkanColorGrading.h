#pragma once

#include "ColorGrading.h"

#include <glm/glm.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;

	// A view's colour grading on the GPU: its lookup table, ColorGradingTableSize texels a side
	// (RGBA16F, 256 KB), an image the bake writes and a texture in the bindless array the tonemap
	// reads. Made at the first frame that grades (VulkanColorGrading::Prepare()), baked again only
	// when its grade moved noticeably, released when the view stops grading, and with the view.
	struct SceneViewColorGrading
	{
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		// The bake's set, writing the table; one of its own pool's
		vk::raii::DescriptorPool Pool{ nullptr };
		vk::DescriptorSet Set;
		// In the bindless array; 0 without a table
		uint32_t Slot = 0;
		// Its grading this frame (the world's component's, under the view's light)
		SceneViewColorGradingSettings Settings;
		// What the table was baked from, once it has been; whether it is baked this frame
		ColorGradingConstants Baked;
		bool bBaked = false;
		bool bBakePending = false;
		// How often it was baked, for tests
		uint32_t BakeCount = 0;
	};

	// Colour grading (Docs/Design/atmosphere.md): the tonemapped picture graded by a lookup table,
	// one lookup a pixel in the tonemap. Each view's table is baked from its grade by a compute
	// shader (ColorGrading.slang) before its picture pass, in the frame's own commands, when the grade
	// changed by more than ColorGradingBakeTolerance; most frames bake nothing.
	class VulkanColorGrading
	{
	public:
		// Both must outlive it
		VulkanColorGrading(VulkanContext& context, ShaderPrograms& programs);

		// The bake's layout and pipeline; the bindless array must outlive it
		void Initialize(VulkanBindless& bindless);
		void Shutdown();
		// Once, and again when ColorGrading.slang changed: every table is baked again
		void CreatePipelines();

		// While no frame is in flight and before this frame's draws are recorded: the view's table,
		// made when it starts grading and released when it stops, and whether it is baked this frame
		void Prepare(SceneViewColorGrading& grading, const SceneViewColorGradingSettings& settings);
		void Release(SceneViewColorGrading& grading);
		// Asks for every table to be baked again at its next frame (the bake's shader changed)
		void Rebake(SceneViewColorGrading& grading);

		// Bakes the view's table if it is due, outside a render pass; it ends in
		// ShaderReadOnlyOptimal layout for the tonemap. Nothing for a view whose table is baked.
		void Bake(SceneViewColorGrading& grading, vk::raii::CommandBuffer& cmd);

		// For tests, between frames: the view's table, x fastest, then y, then z; empty without one
		std::vector<glm::vec4> ReadTable(const SceneViewColorGrading& grading);

	private:
		void MakeTable(SceneViewColorGrading& grading);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;

		vk::raii::DescriptorSetLayout SetLayout{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline BakePipeline{ nullptr };
	};
}

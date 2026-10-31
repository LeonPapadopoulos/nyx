#pragma once

#include "TerrainOcclusion.h"

#include <glm/glm.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>

namespace Nyx
{
	class VulkanBindless;
	class VulkanContext;

	// A view's copy of its world's terrain occlusion volume (TerrainOcclusion.h): a texture a level,
	// R8, a texel a cell at its world cell's index modulo the level's size (sampled wrapping), with
	// the window each holds; made with the first volume it shows, released without one
	struct SceneViewTerrainOcclusion
	{
		struct Level
		{
			vk::raii::Image Image{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView View{ nullptr };
			vk::raii::Buffer Staging{ nullptr };
			vk::raii::DeviceMemory StagingMemory{ nullptr };
			uint32_t Slot = 0;
			// The level's revision in the staging buffer, and whether it is copied this frame
			uint64_t Revision = 0;
			bool bUploadPending = false;
			bool bWritten = false;
			// The corner of the window its texture holds (xyz) and its cells' size (w)
			glm::vec4 Window{ 0.0f };
		};
		std::array<Level, TerrainOcclusion::Levels> Levels;
		// The volume it was last filled from, and how strongly it dims
		const TerrainOcclusionComponent* Source = nullptr;
		float Strength = 0.0f;
		// Copies made, for tests
		uint64_t UploadCount = 0;

		// Every level holds a window: the shaders may sample it
		bool IsDrawn() const
		{
			for (const Level& level : Levels)
			{
				if (level.Slot == 0 || (!level.bWritten && !level.bUploadPending))
				{
					return false;
				}
			}
			return Source != nullptr;
		}
	};

	// The terrain's occlusion volume on the GPU, for the shaders' sky occlusion
	// (nyx.terrainocclusion): each view's levels copied in the frame's own commands when they changed,
	// a level's whole texture at once (256 KB), most frames none.
	class VulkanTerrainOcclusion
	{
	public:
		// The context must outlive it
		explicit VulkanTerrainOcclusion(VulkanContext& context);
		// The bindless array must outlive it
		void Initialize(VulkanBindless& bindless);

		// While no frame is in flight and before this frame's draws are recorded: the view's textures
		// for its world's volume, the levels that changed put in their staging buffers; released
		// without a volume ready
		void Prepare(SceneViewTerrainOcclusion& occlusion, const TerrainOcclusionComponent* volume);
		void Release(SceneViewTerrainOcclusion& occlusion);
		// Copies the levels that changed, outside a render pass; they end in ShaderReadOnlyOptimal
		// layout for the fragment shaders
		void Record(SceneViewTerrainOcclusion& occlusion, vk::raii::CommandBuffer& cmd);

	private:
		void MakeLevel(SceneViewTerrainOcclusion::Level& level);

		VulkanContext& Context;
		VulkanBindless* Bindless = nullptr;
	};
}

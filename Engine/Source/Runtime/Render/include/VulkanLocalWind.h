#pragma once

#include "LocalWind.h"
#include "WindField.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	class VulkanBindless;
	class VulkanContext;

	// A view's texture of local air (LocalWind.h): RGBA16F, a texel a cell, x fastest, then y, then z
	struct SceneViewLocalWind
	{
		WindFieldGrid Grid;
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		vk::raii::Buffer Staging{ nullptr };
		vk::raii::DeviceMemory StagingMemory{ nullptr };
		// Its bindless slot while it holds any air, else 0: the shaders sample nothing
		uint32_t Slot = 0;
		uint32_t TextureSlot = 0;
		// This frame's air of each cell, and the texels last copied to the image
		std::vector<glm::vec3> Velocities;
		std::vector<uint16_t> Texels;
		std::vector<uint16_t> Uploaded;
		bool bUploadPending = false;
		bool bWritten = false;
		// Copies made, for tests and the frame stats
		uint64_t UploadCount = 0;
	};

	// Each view's texture of local air (Docs/Design/wind.md): filled on the CPU from the world's wind
	// field round its camera each frame, and copied to the GPU in the frame's own commands only when
	// what it holds changed; without a field, or with nothing in it, the view has none and the
	// shaders read the world's wind alone.
	class VulkanLocalWind
	{
	public:
		// The context must outlive it
		explicit VulkanLocalWind(VulkanContext& context);

		// The bindless array must outlive it
		void Initialize(VulkanBindless& bindless);

		// While no frame is in flight and before this frame's draws are recorded: the view's texture
		// for `field` round `camera` at the world's `time`, made when its size changes; released
		// without a field
		void Prepare(SceneViewLocalWind& wind, const IWindField* field, const glm::vec3& camera, const LocalWindSettings& settings, float time);
		void Release(SceneViewLocalWind& wind);

		// Copies this frame's air into the texture, if it changed, outside a render pass; it ends in
		// ShaderReadOnlyOptimal layout for the vertex shaders
		void Record(SceneViewLocalWind& wind, vk::raii::CommandBuffer& cmd);

	private:
		void MakeTexture(SceneViewLocalWind& wind);

		VulkanContext& Context;
		VulkanBindless* Bindless = nullptr;
	};
}

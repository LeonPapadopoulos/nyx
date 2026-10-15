#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>

namespace Nyx
{
	// What a mesh is drawn with: a handle VulkanMaterials keeps up to date. The pipeline is its
	// shader's, or the missing material's.
	struct Material
	{
		vk::raii::Pipeline* Pipeline = nullptr;
		// Where its values are in the material buffer (VulkanBindless)
		uint32_t MaterialOffset = 0;
	};
}

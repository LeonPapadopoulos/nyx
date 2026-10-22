#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>

namespace Nyx
{
	// What a mesh is drawn with: a handle VulkanMaterials keeps up to date. The pipelines are its
	// shader's, or the missing material's.
	struct Material
	{
		vk::raii::Pipeline* Pipeline = nullptr;
		// The same material drawn as a decal (SurfaceDecalComponent): over what is drawn already,
		// blended with premultiplied alpha, depth tested without writing depth
		vk::raii::Pipeline* DecalPipeline = nullptr;
		// Where its values are in the material buffer (VulkanBindless)
		uint32_t MaterialOffset = 0;
	};
}

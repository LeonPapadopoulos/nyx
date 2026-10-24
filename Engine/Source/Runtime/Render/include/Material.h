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
		// The decal in the editor's picking pass: its entity's id where the material covers the quad
		vk::raii::Pipeline* DecalPickingPipeline = nullptr;
		// Where its values are in the material buffer (VulkanBindless)
		uint32_t MaterialOffset = 0;
		// Both sides of its triangles drawn, the back one lit as a front (MaterialOptions); only
		// the front otherwise. The draw sets it (SetMeshFaces()).
		bool bTwoSided = false;

		// For a material whose shader has a vertex function (ISurfaceVertex in nyx/surface.slang),
		// the pipelines of the passes besides the scene's that run it too: what casts the primary
		// light's shadows, the editor's picking, and its selection masks, the visible part and the
		// whole silhouette. Null for any other material: the passes' own pipelines draw it.
		vk::raii::Pipeline* ShadowPipeline = nullptr;
		vk::raii::Pipeline* PickingPipeline = nullptr;
		vk::raii::Pipeline* SelectionMaskVisiblePipeline = nullptr;
		vk::raii::Pipeline* SelectionMaskFullPipeline = nullptr;
		// How far its vertex function moves a vertex at most, in metres (MaterialOptions): what the
		// renderer culls a mesh drawn with it by is widened by this much
		float MaxVertexOffset = 0.0f;
	};
}

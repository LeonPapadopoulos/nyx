#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct EnvironmentComponent;
	enum class EAmbientOcclusionQuality : uint8_t;

	// Ambient occlusion as plain math (Docs/Design/atmosphere.md): its size, its samples a quality
	// takes, the view's settings from the environment, and the visible part of one slice of the sky
	// as AmbientOcclusion.slang integrates it. No GPU, so tests check it as the renderer uses it
	// (VulkanAmbientOcclusion).

	// The occlusion's size for a view `width` by `height`: half of it, rounded down, at least a pixel
	glm::uvec2 GetAmbientOcclusionSize(uint32_t width, uint32_t height);

	// The slices round the view direction, and the steps out along each side of a slice, a quality
	// takes: Low 2 and 4, Medium 3 and 6, High 4 and 8
	glm::uvec2 GetAmbientOcclusionSamples(EAmbientOcclusionQuality quality);

	// The view's ambient occlusion from the world's environment, in range
	SceneViewAmbientOcclusionSettings MakeSceneViewAmbientOcclusionSettings(const EnvironmentComponent& environment);

	// The cosine-weighted visible part of a slice through the view, for a normal at angle `n` from
	// the view direction in the slice, and horizons at `h0` and `h1` (angles from the view
	// direction, h0 on one side, h1 on the other, each clamped to within a right angle of the
	// normal), as AmbientOcclusion.slang integrates it: for a surface facing the camera, 1 when
	// nothing rises above it, 0.5 with a wall right beside it on one side. Averaged over the slices
	// round the view, each times the length of the normal projected into it, it is 1 for any open
	// surface.
	float GetSliceVisibility(float n, float h0, float h1);
}

#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct EnvironmentComponent;

	// Bloom's chain as plain math (Docs/Design/atmosphere.md): how many levels a view gets, how big
	// each is, and how much of a colour's light blooms. No GPU, so tests check it as the renderer
	// uses it (VulkanBloom, Bloom.slang).

	inline constexpr uint32_t MaxBloomLevels = 8;
	// The fewest pixels the last level keeps on its shorter side: a smaller one, filtered up across
	// the view, shows its texels as creases in the widest glow
	inline constexpr uint32_t MinBloomLevelSize = 8;

	// Level `level`'s size for a view `width` by `height`: the view halved `level` + 1 times,
	// rounded down, at least a pixel
	glm::uvec2 GetBloomLevelSize(uint32_t width, uint32_t height, uint32_t level);

	// How many levels a view of this size gets when `wanted` are asked for (1 to MaxBloomLevels):
	// no more than leave the last MinBloomLevelSize pixels both ways; at least 1
	uint32_t GetBloomLevelCount(uint32_t width, uint32_t height, uint32_t wanted);

	// The part of a light whose brightest channel is `brightness` that blooms (Bloom.slang's
	// brightPart()): nothing below threshold - knee, all that is above the threshold beyond it,
	// and between, a quadratic that starts smoothly. As a factor of the light: 0 to 1.
	float GetBloomFactor(float brightness, float threshold, float knee);

	// The view's bloom from the world's environment, in range
	SceneViewBloomSettings MakeSceneViewBloomSettings(const EnvironmentComponent& environment);
}

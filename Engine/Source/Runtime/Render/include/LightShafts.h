#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct EnvironmentComponent;

	// Light shafts as plain math (Docs/Design/atmosphere.md): their size, the samples along each
	// pixel's line to the sun, how they fade as the sun leaves the picture or sets, and the view's
	// settings from the environment. No GPU, so tests check it as the renderer uses it
	// (VulkanLightShafts, LightShafts.slang).

	// The samples each pixel takes along its line towards the sun (LightShafts.slang's blur)
	inline constexpr uint32_t LightShaftsSamples = 48;

	// The part of the way from a pixel to the sun at which the light a shaft carries has fallen to
	// a tenth, for a Length of 1
	inline constexpr float LightShaftsTenthFalloff = 0.1f;

	// The sun's place on the picture beyond which it gives no shafts: its farther coordinate, -1 to
	// 1 across the picture, from 1 (its edge, all of them) to this (half a picture past it, none)
	inline constexpr float LightShaftsScreenFadeEnd = 2.0f;

	// The part of the light's own colour the air and the time of day leave of the sun's light,
	// below which its shafts fade out: a smoothstep from 0 to this (about a degree above the
	// horizon in the earth's air)
	inline constexpr float LightShaftsSunFadeEnd = 0.1f;

	// The shafts' size for a view `width` by `height`: half of it, rounded down, at least a pixel
	glm::uvec2 GetLightShaftsSize(uint32_t width, uint32_t height);

	// How much of the shafts a sun at `ndc` on the picture (-1 to 1 across it) gives: 1 on the
	// picture, fading out by LightShaftsScreenFadeEnd
	float GetLightShaftsScreenFade(const glm::vec2& ndc);

	// How much of the shafts a sun gives as it sets: by the part of the light's own colour
	// (`sunLightColor`, as it gives it) the air and the time of day leave (`lightColor`, as it
	// reaches the camera), 1 down to a tenth of it, fading out to none below that
	float GetLightShaftsSunFade(const glm::vec3& lightColor, const glm::vec3& sunLightColor);

	// What each sample along a pixel's line keeps of the one before it, for a Length of `length`
	// and lines `density` of the way to the sun: the light from `length` of the way there falls to
	// a tenth (LightShaftsTenthFalloff)
	float GetLightShaftsDecay(float length, float density);

	// The view's light shafts from the world's environment, in range, under its primary light: the
	// view's `viewProjection`, `towardsLight` (normalised), its colour as it reaches the camera
	// through the air and the time of day (`lightColor`) and as it gives it (`sunLightColor`). Off
	// when the environment's are; on, but drawn only while the sun is on or near the picture,
	// in front of the camera and above the horizon (SceneViewLightShaftsSettings::IsDrawn()).
	SceneViewLightShaftsSettings MakeSceneViewLightShaftsSettings(const EnvironmentComponent& environment, const glm::mat4& viewProjection, const glm::vec3& towardsLight,
		const glm::vec3& lightColor, const glm::vec3& sunLightColor);
}

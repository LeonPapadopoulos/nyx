#pragma once

#include "SceneViewTypes.h"

#include <glm/glm.hpp>

namespace Nyx
{
	struct EnvironmentComponent;

	// The world's air as plain math (Docs/Design/atmosphere.md): how much of the sun's light gets
	// through the air at its height, how bright and how tinted the sky is for it, and how much
	// height fog lies along a ray. No GPU, so tests check it as the renderer uses it; the surface
	// and sky shaders do the same per pixel (nyx.atmosphere).

	// What the sun's light is multiplied by on its way through the air, for a sun `sinElevation`
	// (the sine of its height above the horizon) up: 1 overhead, less and redder lower down,
	// through more air (Kasten and Young's air mass), and none once it has set. `reddening` 0
	// leaves it white; 1 is about the earth's clear air.
	glm::vec3 GetSunTransmittance(float sinElevation, float reddening);

	// How bright the sky is, against the day's, for a sun `sinElevation` up: 1 by day, falling
	// through dusk to `nightBrightness` once the sun is well below the horizon
	float GetDaylight(float sinElevation, float nightBrightness);

	// The colour of light `color` with its brightness taken out: luminance 1, the same hue; white
	// for black
	glm::vec3 GetChromaticity(const glm::vec3& color);

	// The view's air (SceneViewAtmosphere) from the world's environment, in range, under its
	// primary light: `towardsLight` (normalised) and `lightColor` as the light gives them, before
	// the air. The light that reaches the ground, reddened by the air, is the result's SunColor.
	SceneViewAtmosphere MakeSceneViewAtmosphere(const EnvironmentComponent& environment, const glm::vec3& towardsLight, const glm::vec3& lightColor);

	// How much fog lies between `from` and `to`, as an optical depth: the light that gets through
	// is exp(-depth) (nyx::heightFog() works it out the same way). The fog starts at the view's
	// start distance from `from`.
	float GetHeightFogDepth(const SceneViewAtmosphere& atmosphere, const glm::vec3& from, const glm::vec3& to);

	// The part of the light from `to` that reaches `from` through the fog: 1 without fog, never
	// less than 1 - the maximum opacity
	float GetHeightFogTransmittance(const SceneViewAtmosphere& atmosphere, const glm::vec3& from, const glm::vec3& to);

	// The light the fog scatters towards the camera looking along `direction` (normalised): its
	// own colour, and the sun's, gathered round `towardsLight` by its lobe. A fogged colour is
	// colour * T + this * (1 - T), T the transmittance.
	glm::vec3 GetHeightFogInscattering(const SceneViewAtmosphere& atmosphere, const glm::vec3& direction, const glm::vec3& towardsLight);

	// Henyey and Greenstein's phase function: how much light scattered once goes off at an angle
	// whose cosine is `cosine`, for an anisotropy `g` (0 the same every way, near 1 forwards); over
	// the sphere, it sums to 1
	float GetHenyeyGreenstein(float cosine, float g);

	// How far away the sky is for the fog: rays to it are fogged as far as this, in metres
	inline constexpr float SkyFogDistance = 10000.0f;
}

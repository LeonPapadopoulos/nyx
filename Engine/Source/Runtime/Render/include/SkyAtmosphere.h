#pragma once

#include <glm/glm.hpp>

namespace Nyx
{
	struct EnvironmentComponent;

	// The sky made from the sun's place by the light the air scatters, as Hillaire's sky
	// (Docs/Design/atmosphere.md): a planet with a shell of air round it, the world's ground (height
	// 0) on its top, the camera always right above its middle. The world is in metres; the air's
	// math is in kilometres, as Hillaire's is, so that a planet's radius and a camera a few metres
	// up stay apart in single floats: heights above the ground are carried as they are, never as a
	// difference of two radii.

	// The view's air as the lookup tables need it (EnvironmentComponent, in range, in kilometres),
	// under its light, for its camera: MakeSceneViewSkyAtmosphere(). VulkanSkyAtmosphere makes its
	// tables from it; without the Atmosphere mode, bAtmosphere is false and nothing is made.
	struct SceneViewSkyAtmosphere
	{
		bool bAtmosphere = false;

		// Per kilometre at the ground: what the molecules scatter, what haze scatters and takes
		// away, and what the ozone takes away where it is thickest; and how high each reaches
		glm::vec3 RayleighScattering{ 0.0f };
		float RayleighHeight = 8.0f;
		glm::vec3 MieScattering{ 0.0f };
		glm::vec3 MieAbsorption{ 0.0f };
		float MieHeight = 1.2f;
		float MieAnisotropy = 0.8f;
		glm::vec3 OzoneAbsorption{ 0.0f };
		glm::vec3 GroundAlbedo{ 0.3f };
		float PlanetRadius = 6360.0f;
		float AtmosphereHeight = 100.0f;

		// The light at the top of the air: towards the sun (normalised), and its illuminance, pi
		// times the light's colour (what a white surface facing it shows)
		glm::vec3 TowardsSun{ 0.0f, 1.0f, 0.0f };
		glm::vec3 SunIlluminance{ 0.0f };
		// The camera's height above the ground, in kilometres, at least a metre
		float CameraHeight = 0.001f;

		// What the sky's light is multiplied by (GetSkyWarmthTint()), and the faint light it has
		// left once the sun is down (the Night Brightness)
		glm::vec3 WarmthTint{ 1.0f };
		glm::vec3 NightLight{ 0.0f };

		// The sun's disk: the cosine of its angular radius, and its light before the air, as a
		// multiple of the sun's
		float SunDiskCosine = 1.0f;
		glm::vec3 SunDiskColor{ 0.0f };
	};

	// Where the ozone layer is thickest, and how far below and above it reaches, in kilometres
	// (Hillaire's: a tent from 10 to 40 km)
	inline constexpr float OzoneCenterHeight = 25.0f;
	inline constexpr float OzoneHalfWidth = 15.0f;

	// The view's sky from the world's environment, in range, under its primary light (`towardsLight`
	// normalised and `lightColor` as the light gives them, before the air), for a camera at
	// `cameraPosition` (metres; its height is y). Not in the Atmosphere mode, or with Follow Sun off:
	// bAtmosphere false.
	SceneViewSkyAtmosphere MakeSceneViewSkyAtmosphere(const EnvironmentComponent& environment, const glm::vec3& towardsLight, const glm::vec3& lightColor,
		const glm::vec3& cameraPosition);

	// The part of the light that gets through the air from `height` kilometres up along a ray whose
	// cosine with the up axis is `cosZenith`, to the top of the air: 1 over no air, less and redder
	// the more air the ray crosses; none for a ray into the ground. Integrated along the ray as the
	// transmittance table is (TransmittanceSteps steps).
	glm::vec3 GetAtmosphereTransmittance(const SceneViewSkyAtmosphere& sky, float height, float cosZenith);

	// The same over a ray `distance` kilometres long, which may end in the air: 1 at no distance
	glm::vec3 GetAtmosphereTransmittance(const SceneViewSkyAtmosphere& sky, float height, float cosZenith, float distance);

	// How far the horizon is below the level, as the cosine of its angle from the up axis, for a
	// camera `height` kilometres above a planet of `planetRadius`: 0 on the ground, below 0 higher up
	float GetHorizonCosine(float planetRadius, float height);

	// The sun's light where the camera is: through the air from it, and only the part of the sun's
	// disk above the horizon. What lights the world in the Atmosphere mode.
	glm::vec3 GetSunLightThroughAtmosphere(const SceneViewSkyAtmosphere& sky, const glm::vec3& lightColor);

	// What the sky's light is multiplied by for a warmth of `warmth` (-1 to 1): redder and less blue
	// above 0, the other way below, at the same luminance; white at 0
	glm::vec3 GetSkyWarmthTint(float warmth);

	// What of a view's sky must be made again, against what it was made from (`built`): the
	// transmittance and multiple scattering tables when the air changed; the sky view and the sky's
	// light also when the sun turned or changed, or the camera rose or fell noticeably
	// (GetSkyViewHeightTolerance()). Nothing when both are the same.
	struct SkyAtmosphereChanges
	{
		bool bAir = false;
		bool bSkyView = false;
	};
	SkyAtmosphereChanges GetSkyAtmosphereChanges(const SceneViewSkyAtmosphere& built, const SceneViewSkyAtmosphere& wanted);

	// How far the camera may rise or fall, in kilometres, before its sky view is made again: a metre,
	// and 1 % of its height, so that the horizon is never more than a few hundredths of a degree off
	float GetSkyViewHeightTolerance(float height);

	// The transmittance table's steps along a ray, on the GPU and the CPU alike
	inline constexpr int TransmittanceSteps = 40;
}

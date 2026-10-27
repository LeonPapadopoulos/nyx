#pragma once

#include "Wind.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct CloudLayerComponent;

	// The high cloud layer as plain math (Docs/Design/atmosphere.md): a thin shell round the planet
	// at an altitude, planet-centred as the sky's tables are, in kilometres; where a ray from the
	// camera meets it; how the clouds near the camera fade; where the wind has carried the pattern
	// at a world time; the view's settings from the component. No GPU, so tests check it as the
	// renderer (VulkanCloudLayer) and the shaders (nyx.clouds, CloudLayer.slang) use it.

	// The noise the clouds are made from (CloudNoise.slang): a square that tiles, this many texels a
	// side, with its mips down to one texel
	inline constexpr uint32_t CloudNoiseSize = 256;
	inline constexpr uint32_t CloudNoiseLevels = 9;
	// The cloudlets' cells across the square: the square is this many Cloudlet Sizes wide
	inline constexpr float CloudNoiseCells = 16.0f;
	// The squares after which every scale the shaders read the noise at repeats (the largest, the
	// coverage's, is 16 squares wide): where the wind has carried the pattern is kept below it, so
	// it never grows large enough to lose precision on the GPU
	inline constexpr double CloudNoisePeriod = 16.0;

	// How much of the light a kilometre of a cloud's thickest part takes away: a layer of
	// Thickness 0.3 km is 18 deep there, seen straight on, and sends back about two thirds of the
	// sun's light from its top (CloudLayer.slang)
	inline constexpr float CloudExtinction = 60.0f;

	// The view's cloud layer (CloudLayerComponent, in range, for its camera, at its world's time:
	// MakeSceneViewCloudLayerSettings()); none without the component, or with it off
	struct SceneViewCloudLayerSettings
	{
		bool bClouds = false;
		uint32_t Seed = 1;
		// In kilometres above the ground
		float Altitude = 4.0f;
		float Thickness = 0.3f;
		float Coverage = 0.5f;
		float Type = 0.0f;
		float Softness = 0.25f;
		float Detail = 0.35f;
		float Warp = 0.35f;
		float Brightness = 1.0f;
		float Rim = 1.0f;
		float Ambient = 1.0f;
		// How wide the noise's square is over the layer, in kilometres (16 Cloudlet Sizes)
		float TileSize = 6.4f;
		// The way the wind blows, in the world's x and z, normalised: the pattern's first axis; its
		// second is across it (GetCloudPatternPoint())
		glm::vec2 WindAxis{ 1.0f, 0.0f };
		// Where the camera is over the pattern at the world's time, in squares, along the wind and
		// across it, kept between 0 and CloudNoisePeriod
		glm::vec2 Origin{ 0.0f };
		// Whether the sky's cube, the world's light, has the clouds in it (Lights The World and a
		// Light Strength above 0), and how much of them
		bool bLightsWorld = true;
		float LightStrength = 1.0f;
	};

	// How far the pattern may move under the camera, in squares, before the sky's cube, which has
	// the clouds in it, is made again: a 32nd of a cloudlet's cell (12.5 m at the default Cloudlet
	// Size, a wind of 10 m/s every 1.25 s). The cube's texels are 1.4 degrees, 100 m of a layer 4 km
	// overhead: a smaller move doesn't show in its light.
	inline constexpr float CloudCaptureTolerance = 1.0f / (32.0f * CloudNoiseCells);

	// What the sky's cube draws of the cloud layer (VulkanSkyAtmosphere), while it lights the world:
	// the layer's settings for the view, and the noise they are read from in the bindless array
	struct SceneViewCloudCapture
	{
		bool bClouds = false;
		SceneViewCloudLayerSettings Layer;
		uint32_t NoiseSlot = 0;
	};

	// Whether the sky's cube must be made again for its clouds, against what it was made from
	// (`built`): they came or went, a setting or the noise changed, or the pattern moved under the
	// camera by more than CloudCaptureTolerance (the wind, or the camera going sideways). Never
	// while neither has clouds.
	bool IsCloudCaptureChanged(const SceneViewCloudCapture& built, const SceneViewCloudCapture& wanted);

	// The view's cloud layer from the world's first CloudLayerComponent, in range, for a camera at
	// `cameraPosition` (metres) at the world's time `worldTime` (seconds): the wind carries the
	// pattern Wind Speed metres a second of it, so the same seed and time give the same sky
	// The world's wind as it carries clouds at an altitude, the mean wind without gusts or eddies:
	// its way over the ground (normalised), its velocity there now (metres a second), and how far it
	// has carried them by the world's time (metres, not wrapped)
	struct CloudDrift
	{
		glm::vec2 Direction{ 1.0f, 0.0f };
		glm::vec2 Velocity{ 0.0f };
		glm::dvec2 Travel{ 0.0 };
	};

	// The drift of clouds at `altitude` metres in the world's `wind`, which has carried what it
	// carries at its reference height `travel` metres (GetWindTravel()): both scaled by the wind's
	// height profile at that altitude
	CloudDrift GetCloudDrift(const WindState& wind, const glm::dvec2& travel, float altitude);

	// With `drift`, a world's wind at the layer's altitude, the clouds are carried by it unless
	// their component has Own Wind; without one, by their own Wind Direction and Wind Speed
	SceneViewCloudLayerSettings MakeSceneViewCloudLayerSettings(const CloudLayerComponent& clouds, const glm::vec3& cameraPosition, double worldTime,
		const CloudDrift* drift = nullptr);

	// Where a point of the world (x and z in metres) is over the pattern at `worldTime`, in squares,
	// along the wind and across it, not wrapped: what the shaders work out per pixel from Origin
	glm::dvec2 GetCloudPatternPoint(const SceneViewCloudLayerSettings& clouds, const glm::dvec2& worldXZ, double worldTime, float windSpeed);
	// The same for a wind that has carried the pattern `travel` metres over the ground
	glm::dvec2 GetCloudPatternPoint(const SceneViewCloudLayerSettings& clouds, const glm::dvec2& worldXZ, const glm::dvec2& travel);

	// Where a ray meets the layer's shell, `altitude` kilometres up round a planet of `planetRadius`,
	// from a camera `height` kilometres up along a ray whose cosine with the up axis is `cosZenith`:
	// none, one (from below, where it leaves the shell; or from above, entering it, when the ray
	// then hits the ground), or two (from above: in, and out again further on), nearest first. The
	// shell's radius squared less the camera's is taken by the heights, exactly, as the sky's are.
	struct CloudShellHits
	{
		int Count = 0;
		float Distances[2] = { 0.0f, 0.0f };
	};
	CloudShellHits GetCloudShellHits(float planetRadius, float height, float cosZenith, float altitude);

	// How much of the layer shows at a point `distance` kilometres from the camera, for clouds
	// `thickness` kilometres thick: none within a quarter of the thickness, all of it from twice the
	// thickness on, smoothly between. A camera near the layer sees it round itself fade, so it passes
	// through it without a sheet sweeping through the picture: from just below or just above, the
	// clouds straight up or down are near and gone, those further off stay.
	float GetCloudNearFade(float distance, float thickness);
}

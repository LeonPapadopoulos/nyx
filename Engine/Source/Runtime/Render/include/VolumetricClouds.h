#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct VolumetricCloudsComponent;

	// The volumetric clouds as plain math (Docs/Design/atmosphere.md): a band between two shells round
	// the planet, planet-centred as the sky's tables are, in kilometres; where a ray from the camera
	// enters and leaves it; the noises' sizes and where the wind has carried them at a world time;
	// which pixel of each 4 x 4 a frame marches; when everything must be marched again; the view's
	// settings from the component. No GPU, so tests check it as the renderer
	// (VulkanVolumetricClouds) and the shaders (nyx.volumetricclouds, VolumetricClouds.slang) use it.

	// The noises the clouds are made from (VolumetricCloudNoise.slang, VolumetricCloudWeather.slang),
	// each a cube or a square that tiles, this many texels a side, with its mips down to one texel:
	// the shapes' (Perlin-Worley and Worley), the small billows' and wisps' (Worley), the weather map
	inline constexpr uint32_t VolumetricCloudShapeSize = 128;
	inline constexpr uint32_t VolumetricCloudShapeLevels = 8;
	inline constexpr uint32_t VolumetricCloudDetailSize = 32;
	inline constexpr uint32_t VolumetricCloudDetailLevels = 6;
	inline constexpr uint32_t VolumetricCloudWeatherSize = 512;
	inline constexpr uint32_t VolumetricCloudWeatherLevels = 10;
	// Each noise's largest cells across it: it repeats over this many Cloud, Detail or Weather Sizes
	inline constexpr float VolumetricCloudNoiseCells = 4.0f;

	// How much of the light a kilometre of cloud at density 1 takes away (0.04 a metre, about a
	// cumulus's): 200 m of it lets a third through
	inline constexpr float VolumetricCloudExtinction = 40.0f;

	// The clouds are marched at half the view's size each way; with Temporal on, one pixel of every
	// square of this many a side a frame, the others carried from the frame before
	inline constexpr uint32_t VolumetricCloudUpdateSize = 4;
	inline constexpr uint32_t VolumetricCloudUpdateFrames = VolumetricCloudUpdateSize * VolumetricCloudUpdateSize;

	// The view's volumetric clouds (VolumetricCloudsComponent, in range, for its camera, at its world's
	// time: MakeSceneViewVolumetricCloudsSettings()); none without the component, or with it off
	struct SceneViewVolumetricCloudsSettings
	{
		bool bClouds = false;
		uint32_t Seed = 1;
		// The band, in kilometres above the ground
		float Bottom = 1.5f;
		float Top = 5.0f;
		float Coverage = 0.45f;
		float CoverageVariation = 0.6f;
		float Type = 0.75f;
		float TypeVariation = 0.4f;
		float Density = 1.0f;
		// Over how many kilometres each noise repeats: VolumetricCloudNoiseCells times its size
		float ShapeRepeat = 10.0f;
		float DetailRepeat = 1.4f;
		float WeatherRepeat = 48.0f;
		float Detail = 0.6f;
		float Billowiness = 0.8f;
		float EdgeSoftness = 0.15f;
		float Brightness = 1.0f;
		float SilverLining = 1.0f;
		float SilverLiningSpread = 0.4f;
		float Powder = 0.5f;
		float MultipleScattering = 0.5f;
		float Ambient = 1.0f;
		glm::vec3 ShadowTint{ 0.85f, 0.85f, 0.95f };
		// The wind, in kilometres a second of the world's time, in the world's x and z
		glm::vec2 WindVelocity{ 0.0f };
		// Where the camera is over each noise at the world's time, in its repeats, between 0 and 1:
		// the weather map's and the shapes' in x and z, the details' (the shapes' and details' third
		// axis is the height, which the wind doesn't move)
		glm::vec2 WeatherOrigin{ 0.0f };
		glm::vec2 ShapeOrigin{ 0.0f };
		glm::vec2 DetailOrigin{ 0.0f };
		uint32_t Steps = 128;
		uint32_t LightSteps = 6;
		// In kilometres from the camera
		float MaxDistance = 150.0f;
		bool bTemporal = true;
		// EVolumetricCloudsDebug
		uint32_t Debug = 0;
	};

	// The view's volumetric clouds from the world's first VolumetricCloudsComponent, in range, for a
	// camera at `cameraPosition` (metres) at the world's time `worldTime` (seconds): the wind carries
	// the noises Wind Speed metres a second of it, so the same seed and time give the same sky
	SceneViewVolumetricCloudsSettings MakeSceneViewVolumetricCloudsSettings(const VolumetricCloudsComponent& clouds, const glm::vec3& cameraPosition, double worldTime);

	// Where a point of the world (x and z in metres) is over a noise that repeats every `repeat`
	// kilometres at `worldTime`, carried by `windVelocity` (kilometres a second), in repeats, not
	// wrapped: what the shaders work out per sample from the origins
	glm::dvec2 GetVolumetricCloudPatternPoint(const glm::dvec2& worldXZ, double worldTime, const glm::vec2& windVelocity, double repeat);

	// Where a ray from a camera `height` kilometres up, `cosZenith` from the up axis, is inside the
	// band from `bottom` to `top` kilometres round a planet of `planetRadius`: from Enter to Exit
	// along it, both 0 or more; none where it never enters, or meets the ground first. From below,
	// out through the bottom shell, then the top; inside, from the camera to where it leaves; from
	// above, in through the top shell to where it leaves through the bottom or the top again. A ray
	// from above that leaves and enters again past the planet's curve counts only its first span.
	struct CloudBandSpan
	{
		bool bHit = false;
		float Enter = 0.0f;
		float Exit = 0.0f;
	};
	CloudBandSpan GetCloudBandSpan(float planetRadius, float height, float cosZenith, float bottom, float top);

	// Which pixel of each VolumetricCloudUpdateSize square the frame `frame` marches, x and y from 0:
	// in a Bayer matrix's order, so that every pixel is marched once in VolumetricCloudUpdateFrames
	// frames and those of neighbouring frames are far apart
	glm::uvec2 GetVolumetricCloudUpdateOffset(uint32_t frame);

	// What the clouds' pixels are marched with that, if it changes, makes the frames before useless:
	// everything is marched again. The settings without where the wind has carried them (which the
	// pixels carried from the frame before follow), and the way to the sun.
	struct VolumetricCloudsLook
	{
		SceneViewVolumetricCloudsSettings Settings;
		glm::vec3 TowardsSun{ 0.0f };
	};

	// How far the sun may turn in a frame before everything is marched again, in degrees: a pixel is
	// marched again within 16 frames, so a sun turning less lights them all within a tenth of a
	// degree and a half of each other. A time of day running a day in 20 minutes turns it 0.005
	// degrees a frame at 60 frames a second; one dragged in the editor much more.
	inline constexpr float VolumetricCloudSunTolerance = 0.03f;

	// Whether everything must be marched again, against what the carried pixels were marched with
	bool IsVolumetricCloudsLookChanged(const VolumetricCloudsLook& marched, const VolumetricCloudsLook& wanted);
}

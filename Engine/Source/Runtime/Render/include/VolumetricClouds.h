#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct CloudDrift;
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

	// The most pixels a side of the squares of which one pixel a frame is marched (Blocks4x4), the
	// others carried from the frame before; and the frames in which each pixel of such a square is
	// marched once
	inline constexpr uint32_t VolumetricCloudUpdateSize = 4;
	inline constexpr uint32_t VolumetricCloudUpdateFrames = VolumetricCloudUpdateSize * VolumetricCloudUpdateSize;

	// How far the picture may move under the camera in a frame, as a part of its height, before
	// squares of 4 are marched as squares of 2, and before squares of 2 as every pixel: a fast turn or
	// move would otherwise carry more of the picture from where it no longer is, and uncover more
	// that has nothing to be carried from. A part, so a view twice as big marches as much more
	// (1 % is half a degree a frame for a 60-degree-high lens: 30 degrees a second at 60 frames).
	inline constexpr float VolumetricCloudFastShift = 0.01f;
	inline constexpr float VolumetricCloudFasterShift = 0.05f;

	// The view's volumetric clouds (VolumetricCloudsComponent, in range, for its camera, at its world's
	// time: MakeSceneViewVolumetricCloudsSettings()); none without the component, or with it off
	struct SceneViewVolumetricCloudsSettings
	{
		bool bClouds = false;
		uint32_t Seed = 1;
		// The band, in kilometres above the ground
		float Bottom = 0.4f;
		float Top = 1.8f;
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
		float EdgeSoftness = 0.35f;
		float Wispiness = 0.5f;
		float Cellularity = 0.0f;
		float Brightness = 1.0f;
		float SilverLining = 1.0f;
		float SilverLiningSpread = 0.4f;
		float Powder = 0.5f;
		float MultipleScattering = 0.5f;
		float Ambient = 1.0f;
		glm::vec3 SunlitTint{ 1.0f, 0.97f, 0.9f };
		glm::vec3 ShadowTint{ 0.85f, 0.82f, 0.95f };
		float AerialPerspective = 1.5f;
		float Translucency = 0.5f;
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
		float MaxDistance = 80.0f;
		// The squares of which one pixel a frame is marched, pixels a side: 1 (every pixel), 2 or 4
		// (EVolumetricCloudsUpdate); and whether at the view's own size rather than half of it
		uint32_t BlockSize = 4;
		bool bFullResolution = false;
		// EVolumetricCloudsDebug
		uint32_t Debug = 0;
	};

	// The view's volumetric clouds from the world's first VolumetricCloudsComponent, in range, for a
	// camera at `cameraPosition` (metres) at the world's time `worldTime` (seconds): the wind carries
	// the noises Wind Speed metres a second of it, so the same seed and time give the same sky
	// With `drift`, a world's wind at the middle of the band (GetCloudDrift()), the clouds are carried
	// by it unless their component has Own Wind; without one, by their own Wind Direction and Speed
	SceneViewVolumetricCloudsSettings MakeSceneViewVolumetricCloudsSettings(const VolumetricCloudsComponent& clouds, const glm::vec3& cameraPosition, double worldTime,
		const CloudDrift* drift = nullptr);

	// Where a point of the world (x and z in metres) is over a noise that repeats every `repeat`
	// kilometres at `worldTime`, carried by `windVelocity` (kilometres a second), in repeats, not
	// wrapped: what the shaders work out per sample from the origins
	glm::dvec2 GetVolumetricCloudPatternPoint(const glm::dvec2& worldXZ, double worldTime, const glm::vec2& windVelocity, double repeat);
	// The same for a wind that has carried them `travel` metres over the ground
	glm::dvec2 GetVolumetricCloudPatternPoint(const glm::dvec2& worldXZ, const glm::dvec2& travel, double repeat);

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

	// Which pixel of each square `blockSize` pixels a side (1, 2 or 4) the frame `frame` marches, x and
	// y from 0: in a Bayer matrix's order, so that every pixel is marched once in blockSize² frames and
	// those of neighbouring frames are far apart
	glm::uvec2 GetVolumetricCloudUpdateOffset(uint32_t frame, uint32_t blockSize);

	// The squares a frame marches one pixel of, pixels a side: the asked `blockSize`, made smaller
	// while the picture moves under the camera by `shift` (a part of its height) a frame (past
	// VolumetricCloudFastShift, 4 becomes 2; past VolumetricCloudFasterShift, every pixel)
	uint32_t GetVolumetricCloudBlockSize(uint32_t blockSize, float shift);

	// How far the picture moves under the camera from the frame before to this one, in pixels of a
	// picture `height` pixels high: the most any of its corners or its middle moves, turned by the
	// camera's turn (the frame before's rotation and projection, without its place, against this
	// one's), and shifted by the camera's move (`moved` kilometres) as seen at `distance` kilometres,
	// the nearest the clouds may be
	float GetVolumetricCloudScreenShift(const glm::mat4& previousSkyViewProjection, const glm::mat4& skyViewProjection, float height, const glm::vec3& moved, float distance);

	// What the clouds' pixels are marched with that, if it changes, makes the frames before useless:
	// everything is marched again. The settings without where the wind has carried them (which the
	// pixels carried from the frame before follow), and the way to the sun.
	struct VolumetricCloudsLook
	{
		SceneViewVolumetricCloudsSettings Settings;
		glm::vec3 TowardsSun{ 0.0f };
	};

	// How far the sun may turn in a frame before everything is marched again, in degrees: a pixel is
	// marched again within 16 frames at most, so a sun turning less lights them all within a tenth of a
	// degree and a half of each other. A time of day running a day in 20 minutes turns it 0.005
	// degrees a frame at 60 frames a second; one dragged in the editor much more.
	inline constexpr float VolumetricCloudSunTolerance = 0.03f;

	// Whether everything must be marched again, against what the carried pixels were marched with
	bool IsVolumetricCloudsLookChanged(const VolumetricCloudsLook& marched, const VolumetricCloudsLook& wanted);
}

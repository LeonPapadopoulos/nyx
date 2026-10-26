// The volumetric clouds on a real GPU (VulkanVolumetricClouds, VolumetricClouds.slang), under the sky
// made by the air, their world's time set by a clock:
//   - without them the picture is the same to the bit as with them off, or with the sky in the cube
//     map mode, which draws none; and no GPU time;
//   - from the ground by day the sky has clouds (its variance well above the clear sky's), their
//     sunlit sides away from the sun brighter than their shaded sides towards it, their edges
//     towards it brighter with the silver lining than without; at sunset warmer than at noon; at
//     midnight dark, in the night sky's blue;
//   - from above the band, below the horizon the clouds, lit from above, brighter than the ground
//     under the air; straight up the open sky, to the bit;
//   - each preset gives clouds, the small puffs more and smaller than the cumulus;
//   - the frames that carry the clouds from the frames before (one pixel of each 4 x 4 marched a
//     frame) give, once every pixel was marched again, the same picture to the bit as marching them
//     all; after the camera turns, the same within a little; their GPU time a sixteenth or so;
//   - the same seed and time give the same picture, a new view's noises made again; another time
//     another picture, the one a camera moved back against the wind saw before; another seed other
//     clouds;
//   - seen from above them, the ground under them covered;
//   - thick cloud over the sun weakens the light shafts through a window;
//   - their GPU time at 1280 x 720 and 1920 x 1080, the median of 60 frames;
//   - the volumetric clouds test scene, its sun set by its time of day, has its clouds.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there. NyxVolumetricCloudsGpuTests
// --write-scene writes Assets/Scenes/VolumetricCloudsTest.nyxscene; --pictures only renders the test
// scene's pictures of each preset (at noon, at sunset against the sun, from the ground, and lit as its
// reference) into that folder.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ColorGradingComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "FixedStepClock.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "SceneSerializer.h"
#include "TimeOfDay.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "VolumetricCloudsComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;
	using Nyx::RenderTests::Text;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;
	// Frames after which every pixel of the clouds was marched again since a change: 16, and the one
	// that changed
	constexpr int SettledFrames = 18;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("VolumetricClouds-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	float Luminance(const std::array<uint8_t, 3>& rgb)
	{
		return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
	}

	// What the rows `firstRow` to `endRow` of a picture are, together: their luminance's mean, its
	// variance about each row's own mean (so a clear sky's gradient from the horizon up counts for
	// little, patches across it for much), their mean colour
	struct Region
	{
		double Mean = 0.0;
		double Variance = 0.0;
		glm::dvec3 Color{ 0.0 };
	};

	Region Measure(const RgbaImage& image, uint32_t firstRow, uint32_t endRow)
	{
		Region region;
		double sum = 0.0;
		double squares = 0.0;
		size_t count = 0;
		for (uint32_t y = firstRow; y < endRow; ++y)
		{
			double rowSum = 0.0;
			double rowSquares = 0.0;
			for (uint32_t x = 0; x < image.Width; ++x)
			{
				const std::array<uint8_t, 3> rgb = PixelAt(image, x, y);
				const double luminance = Luminance(rgb);
				rowSum += luminance;
				rowSquares += luminance * luminance;
				region.Color += glm::dvec3(rgb[0], rgb[1], rgb[2]);
				++count;
			}
			const double rowMean = rowSum / static_cast<double>(image.Width);
			sum += rowSum;
			squares += rowSquares - rowSum * rowMean;
		}
		region.Mean = sum / static_cast<double>(count);
		region.Variance = squares / static_cast<double>(count);
		region.Color /= static_cast<double>(count);
		return region;
	}

	// The luminance below which a part `q` of the rows `firstRow` to `endRow` lie
	double LuminanceBelow(const RgbaImage& image, uint32_t firstRow, uint32_t endRow, double q)
	{
		std::vector<float> values;
		for (uint32_t y = firstRow; y < endRow; ++y)
		{
			for (uint32_t x = 0; x < image.Width; ++x)
			{
				values.push_back(Luminance(PixelAt(image, x, y)));
			}
		}
		std::sort(values.begin(), values.end());
		return values.empty() ? 0.0 : values[std::min(values.size() - 1, static_cast<size_t>(q * static_cast<double>(values.size())))];
	}

	// The pixels where the clouds are (brighter or darker than the clear sky by more than a few
	// steps): their mean colour in the cloudy picture, and how many there are
	struct Clouds
	{
		glm::dvec3 Color{ 0.0 };
		double Luminance = 0.0;
		size_t Count = 0;
	};

	Clouds MeasureClouds(const RgbaImage& cloudy, const RgbaImage& clear, uint32_t endRow)
	{
		Clouds clouds;
		for (uint32_t y = 0; y < endRow; ++y)
		{
			for (uint32_t x = 0; x < cloudy.Width; ++x)
			{
				const std::array<uint8_t, 3> with = PixelAt(cloudy, x, y);
				const std::array<uint8_t, 3> without = PixelAt(clear, x, y);
				if (std::abs(Luminance(with) - Luminance(without)) > 4.0f)
				{
					clouds.Color += glm::dvec3(with[0], with[1], with[2]);
					clouds.Luminance += Luminance(with);
					++clouds.Count;
				}
			}
		}
		if (clouds.Count > 0)
		{
			clouds.Color /= static_cast<double>(clouds.Count);
			clouds.Luminance /= static_cast<double>(clouds.Count);
		}
		return clouds;
	}

	// The luminance below which a part `q` of the pixels where the clouds are lie, in the rows above
	// `endRow` (MeasureClouds()'s pixels)
	double CloudLuminanceBelow(const RgbaImage& cloudy, const RgbaImage& clear, uint32_t endRow, double q)
	{
		std::vector<float> values;
		for (uint32_t y = 0; y < endRow; ++y)
		{
			for (uint32_t x = 0; x < cloudy.Width; ++x)
			{
				const float with = Luminance(PixelAt(cloudy, x, y));
				if (std::abs(with - Luminance(PixelAt(clear, x, y))) > 4.0f)
				{
					values.push_back(with);
				}
			}
		}
		std::sort(values.begin(), values.end());
		return values.empty() ? 0.0 : values[std::min(values.size() - 1, static_cast<size_t>(q * static_cast<double>(values.size())))];
	}

	// The mean of how much each channel of two pictures differs, and the most
	double MeanDifference(const RgbaImage& a, const RgbaImage& b)
	{
		double sum = 0.0;
		for (size_t index = 0; index < a.Pixels.size(); index += 4)
		{
			for (size_t channel = 0; channel < 3; ++channel)
			{
				sum += std::abs(int(a.Pixels[index + channel]) - int(b.Pixels[index + channel]));
			}
		}
		return sum / static_cast<double>(a.Pixels.size() / 4 * 3);
	}

	int MostDifference(const RgbaImage& a, const RgbaImage& b)
	{
		int most = 0;
		for (size_t index = 0; index < a.Pixels.size(); index += 4)
		{
			for (size_t channel = 0; channel < 3; ++channel)
			{
				most = std::max(most, std::abs(int(a.Pixels[index + channel]) - int(b.Pixels[index + channel])));
			}
		}
		return most;
	}

	std::string Text(const glm::dvec3& color)
	{
		return std::to_string(int(std::lround(color.x))) + " " + std::to_string(int(std::lround(color.y))) + " " + std::to_string(int(std::lround(color.z)));
	}

	TransformComponent Placed(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = rotation, .Scale = scale };
	}

	// A camera looking `yaw` degrees round the up axis from -Z (towards -X for 90), `pitch` up
	glm::quat Looking(float yaw, float pitch)
	{
		return glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	// The yaw (Looking()) of a camera looking the way of `direction` round the up axis
	float YawTowards(const glm::vec3& direction)
	{
		return glm::degrees(std::atan2(-direction.x, -direction.z));
	}

	// The way to a sun `elevation` degrees up, straight along -Z
	glm::vec3 SunAt(float elevation)
	{
		return glm::vec3(0.0f, std::sin(glm::radians(elevation)), -std::cos(glm::radians(elevation)));
	}

	// A light whose light comes from `towardsSun`: its -Z the other way
	glm::quat LightFrom(const glm::vec3& towardsSun)
	{
		return glm::quatLookAt(-glm::normalize(towardsSun), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	// The air's sky alone: no fog, bloom, ambient occlusion or shafts
	EnvironmentComponent SkyOnly()
	{
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.bLightShafts = false;
		return environment;
	}

	// The component's defaults, not a preset's, so the checks don't move as the presets' looks are
	// tuned; the wind along +X
	VolumetricCloudsComponent TestClouds()
	{
		VolumetricCloudsComponent clouds;
		clouds.Coverage = 0.5f;
		clouds.WindDirectionDegrees = 0.0f;
		clouds.WindSpeed = 20.0f;
		return clouds;
	}

	// A sky to look at: a camera, a sun, the air, the clouds, and a clock whose time the world's time
	// is
	struct SkyScene
	{
		Registry World;
		FixedStepClock Clock;
		Entity Camera;
		Entity Sun;
		Entity Air;
		Entity Band;

		explicit SkyScene(VulkanRenderer& renderer)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 10.0f, 0.0f)));
			World.Add<CameraComponent>(Camera, CameraComponent{});

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), LightFrom(SunAt(60.0f))));
			DirectionalLightComponent light;
			light.Intensity = 3.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);

			Air = World.CreateEntity();
			World.Add<EnvironmentComponent>(Air, SkyOnly());
			Band = World.CreateEntity();
			World.Add<VolumetricCloudsComponent>(Band, TestClouds());

			// Paused, so the world is drawn at its last step's time
			SetTime(1000.0);
			renderer.SetWorldInterpolation(&World, &Clock);
		}

		// The world's time, in seconds: one paused step that long
		void SetTime(double seconds)
		{
			Clock = FixedStepClock{};
			Clock.SetPaused(true);
			Clock.SetStepSeconds(seconds);
			Clock.StepOnce();
			Require(Clock.Advance(0.0) == 1 && std::abs(Clock.GetWorldTimeSeconds() - seconds) < 1e-9, "The test's clock can't be set");
		}

		void Look(const glm::vec3& position, float yaw, float pitch)
		{
			TransformComponent& transform = World.Get<TransformComponent>(Camera);
			transform.Position = position;
			transform.Rotation = Looking(yaw, pitch);
		}

		void SetSun(float elevation)
		{
			World.Get<TransformComponent>(Sun).Rotation = LightFrom(SunAt(elevation));
		}

		VolumetricCloudsComponent& Clouds()
		{
			return World.Get<VolumetricCloudsComponent>(Band);
		}

		EnvironmentComponent& Environment()
		{
			return World.Get<EnvironmentComponent>(Air);
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, uint32_t width = ViewWidth, uint32_t height = ViewHeight, float exposureStops = -1.5f)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		renderer.SetSceneViewSky(view, ESceneViewSky::Scene, 0.0f);
		// A sun of intensity 3 lights a white surface 3 times what the screen shows as white, and its
		// clouds near that: a stop and a half darker, so that their shading isn't lost to white
		gpu.Renderer.SetSceneViewExposure(view, exposureStops);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		static_cast<IEditorRenderer&>(gpu.Renderer).DestroySceneView(view);
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// The clouds' picture and the clear sky's, from the same camera
	std::pair<RgbaImage, RgbaImage> CloudyAndClear(SceneRenderer& gpu, SkyScene& scene, uint64_t view, const std::string& name)
	{
		scene.Clouds().bClouds = true;
		const RgbaImage cloudy = Draw(gpu, view);
		scene.Clouds().bClouds = false;
		const RgbaImage clear = Draw(gpu, view);
		scene.Clouds().bClouds = true;
		SaveIfAsked(cloudy, name);
		SaveIfAsked(clear, name + "Clear");
		return { cloudy, clear };
	}

	// Without them, the same to the bit as with them off, or with the sky in the cube map mode; a
	// ground and a box in the picture too, so the surfaces' shading is seen
	void TestUnchangedWithout(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		Mesh* cube = gpu.Renderer.GetCubeMesh();
		Material* grey = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		for (const auto& [position, size] : { std::pair{ glm::vec3(0.0f, -0.5f, -50.0f), glm::vec3(200.0f, 1.0f, 200.0f) }, std::pair{ glm::vec3(3.0f, 2.0f, -15.0f), glm::vec3(4.0f) } })
		{
			const Entity entity = scene.World.CreateEntity();
			scene.World.Add<TransformComponent>(entity, Placed(position, size));
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = cube;
			meshRenderer.MaterialAsset = grey;
			scene.World.Add<MeshRendererComponent>(entity, meshRenderer);
		}
		scene.Look(glm::vec3(0.0f, 2.0f, 0.0f), 0.0f, 15.0f);
		scene.SetSun(35.0f);
		scene.Environment().bFog = true;
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight, 0.0f);

		scene.World.Remove<VolumetricCloudsComponent>(scene.Band);
		const RgbaImage without = Draw(gpu, view);
		SaveIfAsked(without, "Without");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->VolumetricClouds == 0.0f, "A world without volumetric clouds takes GPU time for them");
		VolumetricCloudsComponent off = TestClouds();
		off.bClouds = false;
		scene.World.Add<VolumetricCloudsComponent>(scene.Band, off);
		Require(Draw(gpu, view).Pixels == without.Pixels, "Volumetric clouds that are off change the picture");
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->Shape.Slot == 0 && gpu.Renderer.GetSceneViewGpuTimes(view)->VolumetricClouds == 0.0f,
			"Volumetric clouds that are off keep their noises or take GPU time");

		// On, they change it; the cube map's sky has none
		scene.Clouds().bClouds = true;
		const RgbaImage with = Draw(gpu, view);
		SaveIfAsked(with, "With");
		Require(with.Pixels != without.Pixels, "Volumetric clouds that are on change nothing");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->VolumetricClouds > 0.0f, "The volumetric clouds' GPU time isn't measured");
		scene.Environment().SkyMode = ESkyMode::CubeMap;
		const RgbaImage cubeMap = Draw(gpu, view);
		scene.World.Remove<VolumetricCloudsComponent>(scene.Band);
		Require(Draw(gpu, view).Pixels == cubeMap.Pixels, "The cube map's sky changes with volumetric clouds");
		scene.World.Add<VolumetricCloudsComponent>(scene.Band, TestClouds());
		scene.Environment().SkyMode = ESkyMode::Atmosphere;

		// A material's debug view has none
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::BaseColor);
		gpu.DrawFrames(3);
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->Shape.Slot == 0, "A debug view of the materials draws volumetric clouds");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);

		// The world taken away: their noises and pictures let go of, the device kept
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->Extent == vk::Extent2D(ViewWidth / 2, ViewHeight / 2), "The volumetric clouds' pictures aren't half the view's size");
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(3);
		Require(!gpu.Renderer.IsDeviceLost() && gpu.Renderer.GetSceneViewVolumetricClouds(view)->Extent.width == 0, "A view whose world went away keeps its clouds, or lost the device");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// From the ground: clouds at noon, round the sun brighter; warmer at sunset; dark at midnight
	void TestFromBelow(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		// The rows above the horizon (the picture 60 degrees high, its middle 20 degrees up)
		const uint32_t skyRows = ViewHeight * 2 / 3;

		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 0.0f, 20.0f);
		scene.SetSun(35.0f);
		const auto [towards, towardsClear] = CloudyAndClear(gpu, scene, view, "NoonTowardsSun");
		const Region cloudy = Measure(towards, 0, skyRows);
		const Region clear = Measure(towardsClear, 0, skyRows);
		std::cout << "By day from 10 m the sky's variance is " << cloudy.Variance << " with the clouds, " << clear.Variance << " clear\n";
		Require(cloudy.Variance > 4.0 * clear.Variance + 20.0, "The day sky has no clouds: its variance " + std::to_string(cloudy.Variance) + " against " + std::to_string(clear.Variance));

		// Away from the sun the clouds show their sunlit sides, towards it their shaded ones: brighter
		// away (a thin layer is the other way round)
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 180.0f, 20.0f);
		const auto [away, awayClear] = CloudyAndClear(gpu, scene, view, "NoonAwayFromSun");
		const Clouds shaded = MeasureClouds(towards, towardsClear, skyRows);
		const Clouds sunlit = MeasureClouds(away, awayClear, skyRows);
		std::cout << "By day the clouds towards the sun are " << Text(shaded.Color) << " (" << shaded.Count << " pixels), away from it " << Text(sunlit.Color) << " (" << sunlit.Count
				  << ")\n";
		Require(sunlit.Count > skyRows * ViewWidth / 10 && shaded.Count > skyRows * ViewWidth / 10, "Too few pixels of cloud by day");
		Require(sunlit.Luminance > shaded.Luminance + 5.0, "The clouds' sunlit sides away from the sun aren't brighter than their shaded sides towards it");

		// Towards it, their thin edges glow: the brightest tenth of their pixels brighter with the silver
		// lining than without
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 0.0f, 20.0f);
		scene.Clouds().SilverLining = 0.0f;
		const RgbaImage dull = Draw(gpu, view);
		SaveIfAsked(dull, "NoonTowardsSunNoSilverLining");
		scene.Clouds().SilverLining = TestClouds().SilverLining;
		const double edges = CloudLuminanceBelow(towards, towardsClear, skyRows, 0.9);
		const double dullEdges = CloudLuminanceBelow(dull, towardsClear, skyRows, 0.9);
		std::cout << "Towards the sun, the brightest tenth of the clouds from " << edges << " with the silver lining, " << dullEdges << " without\n";
		Require(edges > dullEdges + 5.0, "Towards the sun, the clouds' edges don't glow with the silver lining");

		// The sun 2 degrees up, out of the picture to the side: warmer than at noon, the same way
		scene.SetSun(60.0f);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 60.0f, 15.0f);
		const auto [noonSide, noonSideClear] = CloudyAndClear(gpu, scene, view, "NoonSide");
		scene.SetSun(2.0f);
		const auto [sunset, sunsetClear] = CloudyAndClear(gpu, scene, view, "Sunset");
		const Clouds noonClouds = MeasureClouds(noonSide, noonSideClear, skyRows);
		const Clouds sunsetClouds = MeasureClouds(sunset, sunsetClear, skyRows);
		const double noonWarmth = (noonClouds.Color.x + 1.0) / (noonClouds.Color.z + 1.0);
		const double sunsetWarmth = (sunsetClouds.Color.x + 1.0) / (sunsetClouds.Color.z + 1.0);
		std::cout << "The clouds at noon " << Text(noonClouds.Color) << ", at sunset " << Text(sunsetClouds.Color) << ": red over blue " << noonWarmth << " and " << sunsetWarmth << "\n";
		Require(sunsetClouds.Count > skyRows * ViewWidth / 10, "Too few pixels of cloud at sunset");
		Require(sunsetWarmth > 1.3 * noonWarmth && sunsetWarmth > 1.0, "The clouds at sunset aren't warmer than at noon");

		// Midnight: dark, the night sky's blue
		scene.SetSun(-40.0f);
		const auto [midnight, midnightClear] = CloudyAndClear(gpu, scene, view, "Midnight");
		const Region night = Measure(midnight, 0, skyRows);
		const Region day = Measure(noonSide, 0, skyRows);
		std::cout << "At midnight the sky with the clouds is " << Text(night.Color) << " on average, at noon " << Text(day.Color) << "\n";
		Require(night.Mean < day.Mean / 4.0 && night.Color.z >= night.Color.x, "The clouds at midnight aren't dark in the night sky's blue");
		Require(MostDifference(midnight, midnightClear) < 40, "The clouds at midnight are lit by more than the night sky");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// The angle above the level of the ray through the middle of a row, for a camera `pitch` up with
	// the default lens (60 degrees high)
	float RowElevation(uint32_t row, float pitch)
	{
		const float ndc = 1.0f - 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(ViewHeight);
		return pitch + glm::degrees(std::atan(ndc * std::tan(glm::radians(30.0f))));
	}

	// From 2 km above the band: below the horizon the clouds, lit from above; above it the open sky
	void TestFromAbove(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		scene.SetSun(50.0f);
		const float top = scene.Clouds().TopAltitude;
		scene.Look(glm::vec3(0.0f, top + 2000.0f, 0.0f), 90.0f, -10.0f);
		const auto [cloudy, clear] = CloudyAndClear(gpu, scene, view, "Above");

		// Above the level, the same to the bit
		uint32_t level = 0;
		while (level < ViewHeight && RowElevation(level, -10.0f) > 0.5f)
		{
			++level;
		}
		Require(level > 20, "The test's camera sees too little above the horizon");
		for (uint32_t y = 0; y < level; ++y)
		{
			for (uint32_t x = 0; x < ViewWidth; ++x)
			{
				Require(PixelAt(cloudy, x, y) == PixelAt(clear, x, y), "Looking up from above the band, the sky has clouds at row " + std::to_string(y));
			}
		}

		// Well below it (from 15 degrees down), the clouds: their sunlit tops (the brightest tenth)
		// brighter than the ground under the air, and patchy
		uint32_t below = 0;
		while (below < ViewHeight && RowElevation(below, -10.0f) > -15.0f)
		{
			++below;
		}
		const Region band = Measure(cloudy, below, ViewHeight);
		const Region ground = Measure(clear, below, ViewHeight);
		const double tops = LuminanceBelow(cloudy, below, ViewHeight, 0.9);
		std::cout << "From 2 km above the band, below the horizon: " << Text(band.Color) << " with the clouds (variance " << band.Variance << ", their brightest tenth from "
				  << tops << "), " << Text(ground.Color) << " without (" << ground.Variance << ")\n";
		Require(tops > ground.Mean + 30.0, "From above, the clouds' tops below the horizon aren't lit");
		Require(band.Variance > ground.Variance + 20.0, "From above, the band below the horizon has no clouds");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Each preset gives clouds; the small puffs more pixels of cloud than the cumulus' open sky
	void TestPresets(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		scene.SetSun(40.0f);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 120.0f, 25.0f);
		const uint32_t skyRows = ViewHeight * 3 / 4;
		std::vector<size_t> counts;
		for (const auto& [preset, name] : { std::pair{ EVolumetricCloudsPreset::SmallPuffs, "SmallPuffs" }, std::pair{ EVolumetricCloudsPreset::Cumulus, "Cumulus" },
				 std::pair{ EVolumetricCloudsPreset::Broken, "Broken" } })
		{
			scene.Clouds() = MakeVolumetricCloudsPreset(preset);
			const auto [cloudy, clear] = CloudyAndClear(gpu, scene, view, std::string("Preset") + name);
			const Clouds clouds = MeasureClouds(cloudy, clear, skyRows);
			const Region region = Measure(cloudy, 0, skyRows);
			std::cout << "The " << name << " preset: " << clouds.Count << " pixels of cloud, " << Text(clouds.Color) << " on average, the sky's variance " << region.Variance << "\n";
			Require(clouds.Count > skyRows * ViewWidth / 8, std::string("The ") + name + " preset has too few clouds");
			counts.push_back(clouds.Count);
		}
		Require(counts[0] > counts[1], "The small puffs don't cover more of the sky than the cumulus");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// One pixel of each 4 x 4 marched a frame, the others carried: once each was marched again, the
	// same to the bit as marching them all; after the camera turns, carried, within a little of it
	void TestTemporal(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		scene.SetSun(30.0f);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 30.0f, 25.0f);

		const RgbaImage first = Draw(gpu, view, 1);
		const uint32_t fullMarches = gpu.Renderer.GetSceneViewVolumetricClouds(view)->FullMarches;
		const RgbaImage settled = Draw(gpu, view, SettledFrames);
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->FullMarches == fullMarches, "A still camera marched every pixel again");
		Require(settled.Pixels == first.Pixels, "A still picture of the clouds, carried, isn't the one with every pixel marched");

		// Turned 3 degrees a frame for 5 frames, then still: carried, against every pixel marched
		for (int frame = 1; frame <= 5; ++frame)
		{
			scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 30.0f + 3.0f * frame, 25.0f);
			gpu.DrawFrames(1);
		}
		RgbaImage turned;
		Require(gpu.Renderer.ReadSceneViewPixels(view, turned), "The scene view can't be read back");
		SaveIfAsked(turned, "TemporalTurned");
		scene.Clouds().bTemporal = false;
		const RgbaImage marched = Draw(gpu, view, 1);
		SaveIfAsked(marched, "TemporalMarched");
		// The left quarter came into view as it turned (15 degrees of 93), its pixels marched one of
		// each 4 x 4 a frame since; the rest was in view before, carried from where it was, keeping
		// the dither of the pixel it was marched at until it is marched again
		const auto columns = [](const RgbaImage& image, uint32_t first)
		{
			RgbaImage part;
			part.Width = image.Width - first;
			part.Height = image.Height;
			for (uint32_t y = 0; y < image.Height; ++y)
			{
				const auto row = image.Pixels.begin() + (static_cast<size_t>(y) * image.Width + first) * 4;
				part.Pixels.insert(part.Pixels.end(), row, row + static_cast<size_t>(part.Width) * 4);
			}
			return part;
		};
		const double seen = MeanDifference(columns(turned, ViewWidth / 4), columns(marched, ViewWidth / 4));
		const double whole = MeanDifference(turned, marched);
		std::cout << "Turning, carried against every pixel marched: " << seen << " on average where it was in view before, " << whole << " over the whole picture\n";
		Require(seen < 2.0 && whole < 3.0, "Carried while the camera turns, the clouds are far from every pixel marched");
		scene.Clouds().bTemporal = true;
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// The same seed and time, the same sky; the wind moves it with the time
	void TestDeterminism(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		uint64_t view = MakeView(gpu);
		scene.SetSun(40.0f);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 45.0f, 35.0f);
		const RgbaImage first = Draw(gpu, view);
		const uint32_t builds = gpu.Renderer.GetSceneViewVolumetricClouds(view)->NoiseBuilds;
		Require(builds == 1, "The volumetric clouds' noises were made " + std::to_string(builds) + " times for one seed");
		EndView(gpu, view);

		// A new view: its noises made again from the seed
		gpu.Renderer.SetWorld(&scene.World);
		view = MakeView(gpu);
		const RgbaImage again = Draw(gpu, view);
		Require(again.Pixels == first.Pixels, "The same seed at the same time doesn't give the same sky");

		// 30 s later: another sky, the one a camera 600 m upwind (the wind blows along +X at 20 m/s)
		// saw 30 s before
		scene.SetTime(1030.0);
		const RgbaImage later = Draw(gpu, view, SettledFrames);
		SaveIfAsked(later, "Later");
		Require(MeanDifference(later, first) > 2.0, "30 s later the wind hasn't moved the clouds");
		scene.SetTime(1000.0);
		scene.Look(glm::vec3(-600.0f, 10.0f, 0.0f), 45.0f, 35.0f);
		const RgbaImage upwind = Draw(gpu, view, SettledFrames);
		std::cout << "30 s later against 600 m upwind 30 s before: " << MeanDifference(later, upwind) << " on average, " << MostDifference(later, upwind) << " at most\n";
		Require(MostDifference(later, upwind) <= 3 && MeanDifference(later, upwind) < 0.05, "The wind doesn't carry the clouds its speed along its way");

		// Another seed, other clouds
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 45.0f, 35.0f);
		scene.Clouds().Seed = 7;
		const RgbaImage seeded = Draw(gpu, view, SettledFrames);
		SaveIfAsked(seeded, "Seed7");
		Require(MeanDifference(seeded, first) > 2.0, "Another seed gives the same clouds");
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->NoiseBuilds == 2, "A new seed didn't make the noises again, once");
		scene.Clouds().Seed = 1;
		Require(Draw(gpu, view, SettledFrames).Pixels == first.Pixels, "The first seed again doesn't give its sky again");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Seen from above the band: the ground below it covered
	void TestGroundUnderTheClouds(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		Mesh* cube = gpu.Renderer.GetCubeMesh();
		Material* grey = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const Entity ground = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(ground, Placed(glm::vec3(0.0f, -0.5f, -4000.0f), glm::vec3(40000.0f, 1.0f, 40000.0f)));
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = cube;
		meshRenderer.MaterialAsset = grey;
		scene.World.Add<MeshRendererComponent>(ground, meshRenderer);
		CameraComponent lens;
		lens.FarPlane = 50000.0f;
		scene.World.Get<CameraComponent>(scene.Camera) = lens;
		scene.Clouds().BottomAltitude = 600.0f;
		scene.Clouds().TopAltitude = 2000.0f;
		scene.Clouds().Coverage = 0.55f;
		scene.SetSun(50.0f);
		scene.Look(glm::vec3(0.0f, 3000.0f, 0.0f), 0.0f, -50.0f);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		const auto [cloudy, clear] = CloudyAndClear(gpu, scene, view, "GroundBelow");
		const Region open = Measure(clear, ViewHeight / 2, ViewHeight);
		const Region covered = Measure(cloudy, ViewHeight / 2, ViewHeight);
		std::cout << "The ground under the clouds, from 3 km: " << Text(open.Color) << " clear, " << Text(covered.Color) << " under the clouds (variance " << open.Variance << " and "
				  << covered.Variance << ")\n";
		Require(MeanDifference(cloudy, clear) > 5.0 && covered.Variance > open.Variance + 20.0, "The ground under the clouds, seen from above them, isn't covered");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// A wall with a window, a low sun behind it, the shafts on: overcast clouds over the sun weaken
	// them against a clear sky
	void TestLightShafts(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		Mesh* cube = gpu.Renderer.GetCubeMesh();
		Material* grey = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const auto box = [&](float left, float right, float bottom, float top)
		{
			const Entity entity = scene.World.CreateEntity();
			scene.World.Add<TransformComponent>(entity, Placed(glm::vec3(0.5f * (left + right), 0.5f * (bottom + top), -20.0f), glm::vec3(right - left, top - bottom, 1.0f)));
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = cube;
			meshRenderer.MaterialAsset = grey;
			scene.World.Add<MeshRendererComponent>(entity, meshRenderer);
		};
		box(-60.0f, 2.0f, -40.0f, 40.0f);
		box(4.0f, 60.0f, -40.0f, 40.0f);
		box(1.9f, 4.1f, 5.0f, 40.0f);
		box(1.9f, 4.1f, -40.0f, 3.0f);
		scene.Look(glm::vec3(0.0f), 0.0f, 0.0f);
		scene.World.Get<TransformComponent>(scene.Sun).Rotation = LightFrom(glm::normalize(glm::vec3(0.0f, 0.2f, -1.0f)));
		scene.Environment().bLightShafts = true;
		scene.Clouds().Coverage = 1.0f;
		scene.Clouds().Density = 3.0f;
		scene.Clouds().Type = 0.0f;
		scene.Clouds().TypeVariation = 0.0f;
		scene.Clouds().BottomAltitude = 800.0f;
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight, 0.0f);

		const CameraComponent lens;
		const std::optional<glm::vec2> beside = WorldToScreenPoint(scene.World.Get<TransformComponent>(scene.Camera), lens, glm::vec2(ViewWidth, ViewHeight), glm::vec3(5.5f, 4.0f, -19.5f));
		Require(beside.has_value(), "The wall beside the window is out of the picture");
		const auto gain = [&](bool bClouds, const std::string& name)
		{
			scene.Clouds().bClouds = bClouds;
			scene.Environment().bLightShafts = true;
			const RgbaImage shafts = Draw(gpu, view);
			SaveIfAsked(shafts, name);
			scene.Environment().bLightShafts = false;
			const RgbaImage plain = Draw(gpu, view);
			const uint32_t x = static_cast<uint32_t>(beside->x);
			const uint32_t y = static_cast<uint32_t>(beside->y);
			return Luminance(PixelAt(shafts, x, y)) - Luminance(PixelAt(plain, x, y));
		};
		const float clear = gain(false, "ShaftsClear");
		const float overcast = gain(true, "ShaftsOvercast");
		std::cout << "The shaft beside the window: " << clear << " brighter under a clear sky, " << overcast << " under overcast clouds\n";
		Require(clear > 10.0f && overcast < 0.5f * clear, "Thick clouds over the sun don't weaken the light shafts");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Their GPU time, the median of 60 frames, at a size: carried (one pixel of each 4 x 4 marched a
	// frame), and every pixel marched; the scene pass's with them and without
	void TestGpuTime(SceneRenderer& gpu, uint32_t width, uint32_t height)
	{
		SkyScene scene(gpu.Renderer);
		scene.SetSun(40.0f);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 30.0f, 20.0f);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, width, height);
		const auto median = [&](float SceneViewGpuTimes::* pass)
		{
			std::vector<float> times;
			gpu.DrawFrames(4);
			for (int frame = 0; frame < 60; ++frame)
			{
				gpu.DrawFrames(1);
				times.push_back(gpu.Renderer.GetSceneViewGpuTimes(view)->*pass);
			}
			std::sort(times.begin(), times.end());
			return times[times.size() / 2];
		};
		const float carried = median(&SceneViewGpuTimes::VolumetricClouds);
		const float scenePass = median(&SceneViewGpuTimes::Scene);
		scene.Clouds().bTemporal = false;
		const float marched = median(&SceneViewGpuTimes::VolumetricClouds);
		scene.Clouds().bTemporal = true;
		// A new seed: its noises made again in the one frame, every pixel marched
		gpu.DrawFrames(4);
		scene.Clouds().Seed = 99;
		gpu.DrawFrames(2);
		const SceneViewGpuTimes withNoise = *gpu.Renderer.GetSceneViewGpuTimes(view);
		gpu.DrawFrames(1);
		std::cout << "At " << width << " x " << height << " the frame that made the noises again took " << withNoise.VolumetricClouds << " ms\n";
		scene.Clouds().bClouds = false;
		const float sceneWithout = median(&SceneViewGpuTimes::Scene);
		std::cout << "At " << width << " x " << height << " the volumetric clouds took " << carried << " ms on the GPU carried (the median of 60 frames), " << marched
				  << " ms with every pixel marched each frame; the scene pass " << scenePass << " ms with them, " << sceneWithout << " ms without\n";
		Require(carried > 0.0f && carried < 2.0f, "The volumetric clouds take more than two milliseconds: " + std::to_string(carried) + " ms");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/VolumetricCloudsTest.nyxscene";

	Entity AddEntity(Registry& world, const char* name, uint64_t guid, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ guid } });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	ColorGradingKey Key(float elevation, const ColorGrade& grade)
	{
		ColorGradingKey key;
		key.SunElevation = elevation;
		key.Grade = grade;
		return key;
	}

	void AddBox(Registry& world, const char* name, uint64_t guid, const glm::vec3& position, const glm::vec3& size, const char* material)
	{
		const Entity entity = AddEntity(world, name, guid, Placed(position, size));
		MeshRendererComponent meshRenderer;
		meshRenderer.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" };
		meshRenderer.Material = AssetReference{ .Type = "Material", .Path = std::string("Materials/VolumetricCloudsTest/") + material + ".nyxmat" };
		world.Add<MeshRendererComponent>(entity, meshRenderer);
	}

	// Cumulus over a town's wall: a ground, a long wall across the view and roofs before it, the camera
	// looking up over them, the time of day in the afternoon, the grade warm and clear by day and
	// gold-green at dusk
	void WriteScene()
	{
		Registry world;
		AddBox(world, "Ground", 0xC10D500001ull, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(4000.0f, 1.0f, 4000.0f), "Field");
		AddBox(world, "Wall", 0xC10D500002ull, glm::vec3(0.0f, 12.0f, -160.0f), glm::vec3(600.0f, 24.0f, 6.0f), "Stone");
		AddBox(world, "House", 0xC10D500003ull, glm::vec3(-38.0f, 5.0f, -40.0f), glm::vec3(22.0f, 10.0f, 14.0f), "Roof");
		AddBox(world, "House 2", 0xC10D500004ull, glm::vec3(36.0f, 6.0f, -46.0f), glm::vec3(20.0f, 12.0f, 16.0f), "Roof");

		const Entity camera = AddEntity(world, "Camera", 0xC10D500005ull, Placed(glm::vec3(0.0f, 1.7f, 0.0f), glm::vec3(1.0f), Looking(0.0f, 14.0f)));
		CameraComponent lens;
		lens.FovYDegrees = 70.0f;
		lens.FarPlane = 5000.0f;
		world.Add<CameraComponent>(camera, lens);

		const Entity sun = AddEntity(world, "Sun", 0xC10D500006ull, Placed(glm::vec3(0.0f)));
		DirectionalLightComponent light;
		light.Intensity = 3.0f;
		world.Add<DirectionalLightComponent>(sun, light);
		const Entity clock = AddEntity(world, "Time Of Day", 0xC10D500007ull, Placed(glm::vec3(0.0f)));
		TimeOfDayComponent timeOfDay;
		timeOfDay.Hours = 15.5f;
		timeOfDay.PathTiltDegrees = 35.0f;
		timeOfDay.SunriseDirectionDegrees = 90.0f;
		timeOfDay.Sun = EntityGuid{ 0xC10D500006ull };
		world.Add<TimeOfDayComponent>(clock, timeOfDay);

		const Entity air = AddEntity(world, "Environment", 0xC10D500008ull, Placed(glm::vec3(0.0f)));
		EnvironmentComponent environment;
		environment.FogDensity = 0.0015f;
		environment.bLightShafts = true;
		world.Add<EnvironmentComponent>(air, environment);

		const Entity band = AddEntity(world, "Volumetric Clouds", 0xC10D500009ull, Placed(glm::vec3(0.0f)));
		VolumetricCloudsComponent clouds = MakeVolumetricCloudsPreset(EVolumetricCloudsPreset::Cumulus);
		clouds.WindDirectionDegrees = 20.0f;
		clouds.WindSpeed = 8.0f;
		world.Add<VolumetricCloudsComponent>(band, clouds);

		// A clear, warm day; the gold-green dusk 3 degrees up; a cool night below the horizon
		ColorGrade night;
		night.Temperature = -0.45f;
		night.Contrast = 1.05f;
		night.Lift = glm::vec3(0.0f, 0.005f, 0.02f);
		night.Saturation = 0.7f;
		ColorGrade goldGreen;
		goldGreen.Temperature = 0.35f;
		goldGreen.Tint = -0.3f;
		goldGreen.Contrast = 1.05f;
		goldGreen.Lift = glm::vec3(0.01f, 0.025f, 0.0f);
		goldGreen.Gamma = glm::vec3(1.02f, 1.06f, 0.95f);
		goldGreen.Saturation = 1.15f;
		goldGreen.ShadowTint = glm::vec3(0.35f, 0.6f, 0.45f);
		goldGreen.ShadowTintStrength = 0.35f;
		goldGreen.HighlightTint = glm::vec3(1.0f, 0.85f, 0.35f);
		goldGreen.HighlightTintStrength = 0.45f;
		goldGreen.TintBalance = 0.1f;
		ColorGrade day;
		day.Temperature = 0.05f;
		day.Contrast = 1.05f;
		day.Saturation = 1.1f;
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Keys = { Key(-8.0f, night), Key(3.0f, goldGreen), Key(40.0f, day) };
		world.Add<ColorGradingComponent>(air, grading);

		const std::filesystem::path assets = Paths::GetAssetsDir();
		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The test scene as the game loads it, its sun turned by its time of day
	struct TestScene
	{
		Registry World;
		FixedStepClock Clock;
		Entity Camera;
		Entity Band;
		Entity TimeOfDay;
		Entity Air;
	};

	void LoadTestScene(SceneRenderer& gpu, TestScene& scene)
	{
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, scene.World, context),
			"The volumetric clouds test scene can't be loaded; NyxVolumetricCloudsGpuTests --write-scene writes it");
		scene.World.Each<VolumetricCloudsComponent>([&](Entity entity, const VolumetricCloudsComponent&) { scene.Band = entity; });
		scene.World.Each<CameraComponent>([&](Entity entity, const CameraComponent&) { scene.Camera = entity; });
		scene.World.Each<TimeOfDayComponent>([&](Entity entity, const TimeOfDayComponent&) { scene.TimeOfDay = entity; });
		scene.World.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent&) { scene.Air = entity; });
		ApplyTimeOfDay(scene.World);
		scene.Clock.SetPaused(true);
		gpu.Renderer.SetWorldInterpolation(&scene.World, &scene.Clock);
	}

	// The test scene as the game loads it: clouds
	void TestVolumetricCloudsTestScene(SceneRenderer& gpu)
	{
		TestScene scene;
		LoadTestScene(gpu, scene);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, 1280, 720, 0.0f);
		const RgbaImage cloudy = Draw(gpu, view, 3);
		SaveIfAsked(cloudy, "TestScene");
		Require(gpu.Renderer.GetSceneViewVolumetricClouds(view)->IsDrawn(), "The volumetric clouds test scene's clouds aren't drawn");
		scene.World.Get<VolumetricCloudsComponent>(scene.Band).bClouds = false;
		const RgbaImage clear = Draw(gpu, view, 2);
		SaveIfAsked(clear, "TestSceneClear");
		const Clouds clouds = MeasureClouds(cloudy, clear, 720);
		std::cout << "The volumetric clouds test scene: " << clouds.Count << " pixels of cloud, " << Text(clouds.Color) << " on average\n";
		Require(clouds.Count > 1280 * 720 / 6, "The volumetric clouds test scene has too few clouds");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// A shot of the test scene: the time of day, where the camera looks (towards the sun's way round
	// the up axis turned by `yaw`), how far up, from what height, and its exposure in stops (the low
	// sun's sky is darker, as a camera's would be at the same exposure)
	struct Shot
	{
		const char* Name;
		float Hours;
		float Yaw;
		float Pitch;
		float Height;
		float Exposure;
	};

	// The test scene's pictures of each preset: lit as its reference, at noon, at sunset against the
	// sun, and from the ground looking over the town; at 1280 x 720, every pixel marched
	void RenderPictures(SceneRenderer& gpu)
	{
		TestScene scene;
		LoadTestScene(gpu, scene);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, 1280, 720, 0.0f);
		struct Look
		{
			EVolumetricCloudsPreset Preset;
			const char* Name;
			Shot Reference;
		};
		const std::array<Look, 3> looks = {
			Look{ EVolumetricCloudsPreset::SmallPuffs, "SmallPuffs", Shot{ "Reference", 17.0f, 150.0f, 22.0f, 1.7f, 0.5f } },
			Look{ EVolumetricCloudsPreset::Cumulus, "Cumulus", Shot{ "Reference", 14.5f, 120.0f, 28.0f, 1.7f, 0.0f } },
			Look{ EVolumetricCloudsPreset::Broken, "Broken", Shot{ "Reference", 17.5f, 0.0f, 9.0f, 1.7f, 0.5f } },
		};
		for (const Look& look : looks)
		{
			const std::array<Shot, 4> shots = {
				look.Reference,
				Shot{ "Noon", 12.0f, 100.0f, 25.0f, 1.7f, 0.0f },
				Shot{ "Sunset", 17.85f, 0.0f, 10.0f, 1.7f, 1.0f },
				Shot{ "Ground", 15.5f, 150.0f, 6.0f, 1.7f, 0.0f },
			};
			VolumetricCloudsComponent& clouds = scene.World.Get<VolumetricCloudsComponent>(scene.Band);
			const VolumetricCloudsComponent preset = MakeVolumetricCloudsPreset(look.Preset);
			clouds = preset;
			clouds.bTemporal = false;
			for (const Shot& shot : shots)
			{
				TimeOfDayComponent& timeOfDay = scene.World.Get<TimeOfDayComponent>(scene.TimeOfDay);
				timeOfDay.Hours = shot.Hours;
				ApplyTimeOfDay(scene.World);
				const glm::vec3 sun = GetSunDirection(timeOfDay, shot.Hours);
				TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
				camera.Position = glm::vec3(0.0f, shot.Height, 0.0f);
				camera.Rotation = Looking(YawTowards(sun) + shot.Yaw, shot.Pitch);
				gpu.Renderer.SetSceneViewExposure(view, shot.Exposure);
				const RgbaImage picture = Draw(gpu, view, 2);
				SaveIfAsked(picture, std::string(look.Name) + "-" + shot.Name);
			}
		}
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		SceneRenderer gpu("NyxVolumetricCloudsGpuTests", MsaaSamplesFromArguments(argc, argv));
		if (argc > 1 && std::strcmp(argv[1], "--pictures") == 0)
		{
			RenderPictures(gpu);
			return 0;
		}
		TestUnchangedWithout(gpu);
		TestFromBelow(gpu);
		TestFromAbove(gpu);
		TestPresets(gpu);
		TestTemporal(gpu);
		TestDeterminism(gpu);
		TestGroundUnderTheClouds(gpu);
		TestLightShafts(gpu);
		TestGpuTime(gpu, 1280, 720);
		TestGpuTime(gpu, 1920, 1080);
		TestVolumetricCloudsTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxVolumetricCloudsGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxVolumetricCloudsGpuTests: all passed\n";
	return 0;
}

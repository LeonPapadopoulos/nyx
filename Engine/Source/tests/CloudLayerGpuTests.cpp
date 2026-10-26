// The high cloud layer on a real GPU (VulkanCloudLayer, CloudLayer.slang), under the sky made by the
// air, its world's time set by a clock:
//   - without the layer the picture is the same to the bit as with it off, or with the sky in the
//     cube map mode, which draws no clouds; and no GPU time;
//   - from 10 m at noon the sky has clouds (its variance well above the clear sky's), brighter
//     towards the sun than away from it; at sunset warmer than at noon; at midnight dark, in the
//     night sky's blue;
//   - from 2 km above the layer, below the horizon the layer, lit from above, brighter than the
//     ground under the air without it; above the horizon the open sky, to the bit;
//   - crossing it: from just below, at it and just above, the pictures close to each other; from
//     10 m below, looking straight up, the clear sky to the bit (the clouds by the camera faded);
//   - the same seed and time give the same picture, a new view's noise made again; another time
//     another picture, the one a camera moved back against the wind saw before; another seed other
//     clouds;
//   - seen from above a low layer, the ground under it covered, a column standing above it not;
//   - the clouds light the world: a white wall the sun doesn't reach, lit by the sky alone, brighter
//     and whiter under a white overcast layer at noon than under a clear sky, warmer at sunset, half
//     way with half the light strength, the same to the bit as without the layer with Lights The
//     World off; a ceiling above a camera 2 km over the layer lit from below by its sunlit tops;
//   - a thick layer over the sun weakens the light shafts through a window;
//   - its GPU time at 1280 x 720 and 1920 x 1080, the median of 60 frames;
//   - the cloud layer test scene, its sun set by its time of day, has its clouds.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there. NyxCloudLayerGpuTests
// --write-scene writes Assets/Scenes/CloudLayerTest.nyxscene.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "CloudLayerComponent.h"
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
	// The layer's altitude in the tests, metres
	constexpr float Altitude = 4000.0f;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("CloudLayer-" + name + ".png");
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

	CloudLayerComponent Mackerel()
	{
		CloudLayerComponent clouds;
		clouds.Altitude = Altitude;
		clouds.Coverage = 0.55f;
		clouds.WindDirectionDegrees = 0.0f;
		clouds.WindSpeed = 20.0f;
		return clouds;
	}

	// A sky to look at: a camera, a sun, the air, a cloud layer, and a clock whose time the world's
	// time is
	struct SkyScene
	{
		Registry World;
		FixedStepClock Clock;
		Entity Camera;
		Entity Sun;
		Entity Air;
		Entity Layer;

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
			Layer = World.CreateEntity();
			World.Add<CloudLayerComponent>(Layer, Mackerel());

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

		CloudLayerComponent& Clouds()
		{
			return World.Get<CloudLayerComponent>(Layer);
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

	// Without the layer, the same to the bit as with it off, or with the sky in the cube map mode;
	// a ground and a box in the picture too, so the surfaces' shading is seen
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

		scene.World.Remove<CloudLayerComponent>(scene.Layer);
		const RgbaImage without = Draw(gpu, view);
		SaveIfAsked(without, "Without");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->Clouds == 0.0f, "A world without a cloud layer takes GPU time for it");
		CloudLayerComponent off = Mackerel();
		off.bClouds = false;
		scene.World.Add<CloudLayerComponent>(scene.Layer, off);
		Require(Draw(gpu, view).Pixels == without.Pixels, "A cloud layer that is off changes the picture");
		Require(gpu.Renderer.GetSceneViewCloudLayer(view)->NoiseSlot == 0 && gpu.Renderer.GetSceneViewGpuTimes(view)->Clouds == 0.0f, "A cloud layer that is off keeps its noise or takes GPU time");

		// On, it changes it; the cube map's sky has none
		scene.Clouds().bClouds = true;
		const RgbaImage with = Draw(gpu, view);
		SaveIfAsked(with, "With");
		Require(with.Pixels != without.Pixels, "A cloud layer that is on changes nothing");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->Clouds > 0.0f, "The cloud layer's GPU time isn't measured");
		scene.Environment().SkyMode = ESkyMode::CubeMap;
		const RgbaImage cubeMap = Draw(gpu, view);
		scene.World.Remove<CloudLayerComponent>(scene.Layer);
		Require(Draw(gpu, view).Pixels == cubeMap.Pixels, "The cube map's sky changes with a cloud layer");
		scene.World.Add<CloudLayerComponent>(scene.Layer, Mackerel());
		scene.Environment().SkyMode = ESkyMode::Atmosphere;

		// A material's debug view has none
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::BaseColor);
		gpu.DrawFrames(3);
		Require(gpu.Renderer.GetSceneViewCloudLayer(view)->NoiseSlot == 0, "A debug view of the materials draws clouds");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);

		// The world taken away: its noise and target let go of, the device kept
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewCloudLayer(view)->Extent == vk::Extent2D(ViewWidth, ViewHeight), "The cloud layer's target isn't the view's size");
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(3);
		Require(!gpu.Renderer.IsDeviceLost() && gpu.Renderer.GetSceneViewCloudLayer(view)->Extent.width == 0, "A view whose world went away keeps its clouds, or lost the device");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// From 10 m: clouds at noon, brighter towards the sun; warmer at sunset; dark at midnight
	void TestFromBelow(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		// The rows above the horizon (the picture 60 degrees high, its middle 20 degrees up)
		const uint32_t skyRows = ViewHeight * 2 / 3;

		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 0.0f, 20.0f);
		scene.SetSun(60.0f);
		const auto [towards, towardsClear] = CloudyAndClear(gpu, scene, view, "NoonTowardsSun");
		const Region cloudy = Measure(towards, 0, skyRows);
		const Region clear = Measure(towardsClear, 0, skyRows);
		std::cout << "At noon from 10 m the sky's variance is " << cloudy.Variance << " with the clouds, " << clear.Variance << " clear\n";
		Require(cloudy.Variance > 4.0 * clear.Variance + 20.0, "The noon sky has no clouds: its variance " + std::to_string(cloudy.Variance) + " against " + std::to_string(clear.Variance));

		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 180.0f, 20.0f);
		const auto [away, awayClear] = CloudyAndClear(gpu, scene, view, "NoonAwayFromSun");
		const Clouds sunlit = MeasureClouds(towards, towardsClear, skyRows);
		const Clouds shaded = MeasureClouds(away, awayClear, skyRows);
		std::cout << "At noon the clouds towards the sun are " << Text(sunlit.Color) << " (" << sunlit.Count << " pixels), away from it " << Text(shaded.Color) << " (" << shaded.Count
				  << ")\n";
		Require(sunlit.Count > skyRows * ViewWidth / 10 && shaded.Count > skyRows * ViewWidth / 10, "Too few pixels of cloud at noon");
		Require(sunlit.Luminance > shaded.Luminance + 5.0, "The clouds towards the sun aren't brighter than away from it");

		// The sun 2 degrees up, out of the picture to the side: warmer than at noon, the same way
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
		Require(sunsetWarmth > 1.5 * noonWarmth && sunsetWarmth > 1.0, "The clouds at sunset aren't warmer than at noon");

		// Midnight: dark, the night sky's blue
		scene.SetSun(-40.0f);
		const auto [midnight, midnightClear] = CloudyAndClear(gpu, scene, view, "Midnight");
		const Region night = Measure(midnight, 0, skyRows);
		const Region day = Measure(noonSide, 0, skyRows);
		std::cout << "At midnight the sky with the clouds is " << Text(night.Color) << " on average, at noon " << Text(day.Color) << "\n";
		Require(night.Mean < day.Mean / 4.0 && night.Color.z > night.Color.x, "The clouds at midnight aren't dark in the night sky's blue");
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

	// From 2 km above the layer: below the horizon the layer, lit from above; above it the open sky
	void TestFromAbove(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		scene.SetSun(50.0f);
		scene.Look(glm::vec3(0.0f, Altitude + 2000.0f, 0.0f), 90.0f, -10.0f);
		const auto [cloudy, clear] = CloudyAndClear(gpu, scene, view, "Above");

		// Above the level (the horizon is 2.5 degrees below it at 6 km), the same to the bit
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
				Require(PixelAt(cloudy, x, y) == PixelAt(clear, x, y), "Looking up from above the layer, the sky has clouds at row " + std::to_string(y));
			}
		}

		// Well below it (from 15 degrees down), the layer: lit from above, brighter than the ground
		// under the air, and patchy
		uint32_t below = 0;
		while (below < ViewHeight && RowElevation(below, -10.0f) > -15.0f)
		{
			++below;
		}
		const Region layer = Measure(cloudy, below, ViewHeight);
		const Region ground = Measure(clear, below, ViewHeight);
		std::cout << "From 2 km above the layer, below the horizon: " << Text(layer.Color) << " with the clouds (variance " << layer.Variance << "), " << Text(ground.Color)
				  << " without (" << ground.Variance << ")\n";
		Require(layer.Mean > ground.Mean + 15.0, "From above, the layer below the horizon isn't lit");
		Require(layer.Variance > ground.Variance + 20.0, "From above, the layer below the horizon has no clouds");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Through the layer: no pop at its height, nothing by the camera, the clouds further off kept
	void TestCrossing(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		scene.SetSun(30.0f);
		scene.Clouds().Coverage = 0.7f;

		// Level, to the side of the sun, from 20 m below to 20 m above
		std::vector<RgbaImage> pictures;
		const std::array<float, 5> heights = { -20.0f, -5.0f, 0.0f, 5.0f, 20.0f };
		for (const float height : heights)
		{
			scene.Look(glm::vec3(0.0f, Altitude + height, 0.0f), 90.0f, 0.0f);
			pictures.push_back(Draw(gpu, view));
			SaveIfAsked(pictures.back(), "Crossing" + std::to_string(int(height)));
		}
		scene.Look(glm::vec3(0.0f, Altitude - 2000.0f, 0.0f), 90.0f, 0.0f);
		const RgbaImage farBelow = Draw(gpu, view);
		scene.Look(glm::vec3(0.0f, Altitude + 2000.0f, 0.0f), 90.0f, 0.0f);
		const RgbaImage farAbove = Draw(gpu, view);
		// The distant clouds above the level narrow to a band at the horizon as the camera rises to
		// the layer, and from above it the near clouds below fade in: smoothly, the least change at
		// the layer's height itself
		const double far = MeanDifference(farBelow, farAbove);
		std::cout << "From 2 km below to 2 km above the layer the picture changes by " << far << " on average\n";
		for (size_t index = 1; index < pictures.size(); ++index)
		{
			const double step = MeanDifference(pictures[index - 1], pictures[index]);
			std::cout << "Crossing the layer, from " << heights[index - 1] << " m to " << heights[index] << " m: the picture changes by " << step << " on average, "
					  << MostDifference(pictures[index - 1], pictures[index]) << " at most\n";
			const bool bAtTheLayer = heights[index - 1] >= -5.0f && heights[index] <= 5.0f;
			Require(step < far / 6.0 && (!bAtTheLayer || step < 1.0),
				"The picture pops crossing the layer, from " + std::to_string(heights[index - 1]) + " m to " + std::to_string(heights[index]) + " m");
		}
		Require(MeanDifference(pictures[1], pictures[3]) < 1.5, "Just below and just above the layer, the pictures aren't alike");

		// Straight up from 10 m below: the clouds by the camera gone, the clear sky to the bit; from
		// 2 km below, clouds
		scene.Look(glm::vec3(0.0f, Altitude - 10.0f, 0.0f), 0.0f, 90.0f);
		const auto [justBelow, justBelowClear] = CloudyAndClear(gpu, scene, view, "UpFromJustBelow");
		Require(justBelow.Pixels == justBelowClear.Pixels, "Straight up from 10 m below the layer, the clouds by the camera aren't faded out");
		scene.Look(glm::vec3(0.0f, Altitude - 2000.0f, 0.0f), 0.0f, 90.0f);
		const auto [up, upClear] = CloudyAndClear(gpu, scene, view, "UpFromBelow");
		Require(MeanDifference(up, upClear) > 5.0, "Straight up from 2 km below the layer, there are no clouds");
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
		const uint32_t builds = gpu.Renderer.GetSceneViewCloudLayer(view)->NoiseBuilds;
		Require(builds == 1, "The cloud layer's noise was made " + std::to_string(builds) + " times for one seed");
		EndView(gpu, view);

		// A new view: its noise made again from the seed
		gpu.Renderer.SetWorld(&scene.World);
		view = MakeView(gpu);
		const RgbaImage again = Draw(gpu, view);
		Require(again.Pixels == first.Pixels, "The same seed at the same time doesn't give the same sky");

		// 30 s later: another sky, the one a camera 600 m upwind (the wind blows along +X at
		// 20 m/s) saw 30 s before
		scene.SetTime(1030.0);
		const RgbaImage later = Draw(gpu, view);
		SaveIfAsked(later, "Later");
		Require(MeanDifference(later, first) > 2.0, "30 s later the wind hasn't moved the clouds");
		scene.SetTime(1000.0);
		scene.Look(glm::vec3(-600.0f, 10.0f, 0.0f), 45.0f, 35.0f);
		const RgbaImage upwind = Draw(gpu, view);
		std::cout << "30 s later against 600 m upwind 30 s before: " << MeanDifference(later, upwind) << " on average, " << MostDifference(later, upwind) << " at most\n";
		Require(MostDifference(later, upwind) <= 3 && MeanDifference(later, upwind) < 0.05, "The wind doesn't carry the clouds its speed along its way");

		// Another seed, other clouds
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 45.0f, 35.0f);
		scene.Clouds().Seed = 7;
		const RgbaImage seeded = Draw(gpu, view);
		SaveIfAsked(seeded, "Seed7");
		Require(MeanDifference(seeded, first) > 2.0, "Another seed gives the same clouds");
		Require(gpu.Renderer.GetSceneViewCloudLayer(view)->NoiseBuilds == 2, "A new seed didn't make the noise again, once");
		scene.Clouds().Seed = 1;
		Require(Draw(gpu, view).Pixels == first.Pixels, "The first seed again doesn't give its sky again");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Seen from above a low layer: the ground below it covered, a column standing above it not
	void TestGroundUnderTheLayer(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		Mesh* cube = gpu.Renderer.GetCubeMesh();
		Material* grey = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		for (const auto& [position, size] : { std::pair{ glm::vec3(0.0f, -0.5f, -800.0f), glm::vec3(4000.0f, 1.0f, 4000.0f) }, std::pair{ glm::vec3(0.0f, 200.0f, -600.0f), glm::vec3(60.0f, 400.0f, 60.0f) } })
		{
			const Entity entity = scene.World.CreateEntity();
			scene.World.Add<TransformComponent>(entity, Placed(position, size));
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = cube;
			meshRenderer.MaterialAsset = grey;
			scene.World.Add<MeshRendererComponent>(entity, meshRenderer);
		}
		CameraComponent lens;
		lens.FarPlane = 5000.0f;
		scene.World.Get<CameraComponent>(scene.Camera) = lens;
		scene.Clouds().Altitude = 300.0f;
		scene.Clouds().Thickness = 60.0f;
		scene.Clouds().Coverage = 0.6f;
		scene.Clouds().CloudletSize = 60.0f;
		// The column's light the same with the clouds and without
		scene.Clouds().bLightsWorld = false;
		scene.SetSun(50.0f);
		const glm::vec3 eye(0.0f, 800.0f, 0.0f);
		scene.Look(eye, 0.0f, -38.0f);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu);
		const auto [cloudy, clear] = CloudyAndClear(gpu, scene, view, "GroundBelow");

		// The column's top, 400 m up, above the layer: the same; the ground beyond it covered
		const TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
		const std::optional<glm::vec2> top = WorldToScreenPoint(camera, lens, glm::vec2(ViewWidth, ViewHeight), glm::vec3(0.0f, 400.0f, -600.0f));
		Require(top.has_value(), "The column's top is out of the picture");
		const uint32_t topX = static_cast<uint32_t>(top->x);
		const uint32_t topY = static_cast<uint32_t>(top->y) + 2;
		Require(PixelAt(cloudy, topX, topY) == PixelAt(clear, topX, topY), "The column's top above the layer is covered by its clouds");
		const Region ground = Measure(clear, ViewHeight * 3 / 4, ViewHeight);
		const Region covered = Measure(cloudy, ViewHeight * 3 / 4, ViewHeight);
		std::cout << "The ground under the layer, from 800 m: " << Text(ground.Color) << " clear, " << Text(covered.Color) << " under the clouds (variance " << ground.Variance << " and "
				  << covered.Variance << ")\n";
		Require(MeanDifference(cloudy, clear) > 5.0 && covered.Variance > ground.Variance + 20.0, "The ground under the layer, seen from above it, isn't covered by its clouds");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// How blue a picture is on average, and how warm: blue over red, red over blue
	double Blueness(const Region& region)
	{
		return (region.Color.z + 1.0) / (region.Color.x + 1.0);
	}

	// The clouds in the sky's light on the world. A white wall filling the picture, the sun behind
	// it, so it is lit by the sky alone: brighter and whiter under a white overcast layer at noon than
	// under a clear sky, warmer at sunset than at noon, half way with half the light strength, and
	// with Lights The World off the same to the bit as without the layer. A camera 2 km above the
	// layer looking up at a ceiling: lit from below by the layer's sunlit tops.
	void TestLightsTheWorld(SceneRenderer& gpu)
	{
		LiveMaterialFile white("Materials/NyxCloudLayerGpuTests-White.nyxmat");
		white.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.8 0.8 0.8\nRoughness = 1\n");
		SkyScene scene(gpu.Renderer);
		const Entity wall = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(wall, Placed(glm::vec3(0.0f, 10.0f, -10.0f), glm::vec3(400.0f, 400.0f, 1.0f)));
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = gpu.Renderer.GetCubeMesh();
		meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial(white.Path);
		scene.World.Add<MeshRendererComponent>(wall, meshRenderer);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 0.0f, 0.0f);
		scene.SetSun(60.0f);
		scene.Clouds().Altitude = 2000.0f;
		scene.Clouds().Coverage = 1.0f;
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight, 0.0f);

		// Noon: clear, overcast without and with its light
		scene.World.Remove<CloudLayerComponent>(scene.Layer);
		const RgbaImage clearNoon = Draw(gpu, view);
		SaveIfAsked(clearNoon, "WallClearNoon");
		CloudLayerComponent overcast = Mackerel();
		overcast.Altitude = 2000.0f;
		overcast.Coverage = 1.0f;
		overcast.bLightsWorld = false;
		scene.World.Add<CloudLayerComponent>(scene.Layer, overcast);
		const RgbaImage unlit = Draw(gpu, view);
		Require(unlit.Pixels == clearNoon.Pixels, "With Lights The World off, the clouds change the world's light");
		scene.Clouds().bLightsWorld = true;
		const RgbaImage overcastNoon = Draw(gpu, view);
		SaveIfAsked(overcastNoon, "WallOvercastNoon");
		scene.Clouds().LightStrength = 0.5f;
		const RgbaImage halfNoon = Draw(gpu, view);
		scene.Clouds().LightStrength = 1.0f;
		const Region clear = Measure(clearNoon, 0, ViewHeight);
		const Region cloudy = Measure(overcastNoon, 0, ViewHeight);
		const Region half = Measure(halfNoon, 0, ViewHeight);
		std::cout << "A white wall lit by the sky alone at noon: " << Text(clear.Color) << " under a clear sky, " << Text(cloudy.Color) << " under a white overcast layer, "
				  << Text(half.Color) << " with half its light\n";
		Require(cloudy.Mean > clear.Mean + 5.0, "Under a white overcast layer the wall lit by the sky isn't brighter");
		Require(Blueness(cloudy) < Blueness(clear), "Under a white overcast layer the wall lit by the sky isn't whiter");
		Require(half.Mean > clear.Mean && half.Mean < cloudy.Mean, "Half the light strength isn't between none and all");

		// Sunset: the layer's gold warms it
		scene.SetSun(2.0f);
		const RgbaImage overcastSunset = Draw(gpu, view);
		SaveIfAsked(overcastSunset, "WallOvercastSunset");
		scene.Clouds().bLightsWorld = false;
		const Region clearSunset = Measure(Draw(gpu, view), 0, ViewHeight);
		scene.Clouds().bLightsWorld = true;
		const Region sunset = Measure(overcastSunset, 0, ViewHeight);
		std::cout << "At sunset: " << Text(sunset.Color) << " under the layer (red over blue " << 1.0 / Blueness(sunset) << "), " << Text(clearSunset.Color)
				  << " lit as without it (" << 1.0 / Blueness(clearSunset) << "); at noon under the layer " << 1.0 / Blueness(cloudy) << "\n";
		Require(Blueness(sunset) < Blueness(cloudy) && Blueness(sunset) < Blueness(clearSunset), "At sunset the layer doesn't warm the wall lit by the sky");

		// From 2 km above the layer, looking up at a ceiling 20 m over the camera: lit from below by
		// the layer's sunlit tops, brighter than by the ground under the air
		scene.SetSun(60.0f);
		const glm::vec3 eye(0.0f, 4000.0f, 0.0f);
		scene.World.Get<TransformComponent>(wall) = Placed(eye + glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(400.0f, 1.0f, 400.0f));
		scene.Look(eye, 0.0f, 90.0f);
		scene.Clouds().bLightsWorld = false;
		const Region groundLit = Measure(Draw(gpu, view, 3), 0, ViewHeight);
		scene.Clouds().bLightsWorld = true;
		const RgbaImage ceiling = Draw(gpu, view, 3);
		SaveIfAsked(ceiling, "CeilingAbove");
		const Region cloudLit = Measure(ceiling, 0, ViewHeight);
		std::cout << "A ceiling 2 km above the layer, lit from below: " << Text(cloudLit.Color) << " by the clouds, " << Text(groundLit.Color) << " by the ground under the air\n";
		Require(cloudLit.Mean > groundLit.Mean + 5.0, "Above the layer the clouds below don't light the world from below");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// The sky's cube made again as the wind moves the clouds: not every frame, and its time
	void TestCaptureCost(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		scene.Look(glm::vec3(0.0f, 10.0f, 0.0f), 30.0f, 20.0f);
		scene.SetSun(40.0f);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, 1280, 720);
		gpu.DrawFrames(4);

		// A minute of the world's time at 20 m/s in steps of a 60th of a second: a step moves the
		// pattern 0.33 m, so the cube is made again about every 38 steps
		const uint32_t before = gpu.Renderer.GetSceneViewSkyTables(view)->CubeBuilds;
		std::vector<float> rebuilds;
		for (int step = 1; step <= 600; ++step)
		{
			scene.SetTime(1000.0 + step / 60.0);
			const uint32_t builds = gpu.Renderer.GetSceneViewSkyTables(view)->CubeBuilds;
			gpu.DrawFrames(1);
			if (gpu.Renderer.GetSceneViewSkyTables(view)->CubeBuilds != builds)
			{
				gpu.DrawFrames(1);
				rebuilds.push_back(gpu.Renderer.GetSceneViewGpuTimes(view)->Sky);
			}
		}
		const uint32_t made = gpu.Renderer.GetSceneViewSkyTables(view)->CubeBuilds - before;
		std::sort(rebuilds.begin(), rebuilds.end());
		const float median = rebuilds.empty() ? 0.0f : rebuilds[rebuilds.size() / 2];
		std::cout << "With a wind of 20 m/s, the sky's cube was made again " << made << " times in 600 steps, " << median << " ms each (the median)\n";
		Require(made >= 10 && made <= 30, "The sky's cube isn't made again as the clouds move, or too often: " + std::to_string(made) + " times in 600 steps");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// A wall with a window, a low sun behind it, the shafts on: an overcast layer over the sun weakens
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
		scene.Clouds().Altitude = 2000.0f;
		scene.Clouds().Coverage = 1.0f;
		scene.Clouds().Thickness = 1500.0f;
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
		std::cout << "The shaft beside the window: " << clear << " brighter under a clear sky, " << overcast << " under an overcast layer\n";
		Require(clear > 10.0f && overcast < 0.5f * clear, "A thick layer over the sun doesn't weaken the light shafts");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	// Its GPU time, the median of 60 frames, at a size; and the scene pass's with it and without
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
		const float clouds = median(&SceneViewGpuTimes::Clouds);
		const float scenePass = median(&SceneViewGpuTimes::Scene);
		scene.Clouds().Type = 0.5f;
		const float blended = median(&SceneViewGpuTimes::Clouds);
		// A new seed: its noise made again in the one frame, before the sky's tables, whose cube it is
		// drawn into, made again for it too
		scene.Clouds().Type = 0.0f;
		gpu.DrawFrames(4);
		scene.Clouds().Seed = 99;
		gpu.DrawFrames(2);
		const SceneViewGpuTimes withNoise = *gpu.Renderer.GetSceneViewGpuTimes(view);
		std::cout << "At " << width << " x " << height << " the frame that made the noise again took " << withNoise.Sky << " ms for the noise and the sky's cube, "
				  << withNoise.Clouds << " ms for the cloud layer\n";
		scene.Clouds().bClouds = false;
		const float sceneWithout = median(&SceneViewGpuTimes::Scene);
		std::cout << "At " << width << " x " << height << " the cloud layer took " << clouds << " ms on the GPU (the median of 60 frames), " << blended
				  << " ms with both shapes blended; the scene pass " << scenePass << " ms with it, " << sceneWithout << " ms without\n";
		Require(clouds > 0.0f && clouds < 1.0f, "The cloud layer takes more than a millisecond: " + std::to_string(clouds) + " ms");
		gpu.Renderer.SetWorldInterpolation(&scene.World, nullptr);
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/CloudLayerTest.nyxscene";

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

	// A mackerel sky at dusk (the plan's frame 1): a ground, the camera looking up over it towards
	// the low sun, a field of small cloudlets receding to the horizon, the time of day at 17:44 with
	// the sun 3 degrees up, the grade gold-green at that height
	void WriteScene()
	{
		Registry world;
		const Entity ground = AddEntity(world, "Ground", 0xC10D000001ull, Placed(glm::vec3(0.0f, -0.5f, -100.0f), glm::vec3(600.0f, 1.0f, 600.0f)));
		MeshRendererComponent meshRenderer;
		meshRenderer.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" };
		meshRenderer.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" };
		world.Add<MeshRendererComponent>(ground, meshRenderer);

		const Entity camera = AddEntity(world, "Camera", 0xC10D000002ull, Placed(glm::vec3(0.0f, 1.7f, 0.0f), glm::vec3(1.0f), Looking(20.0f, 22.0f)));
		world.Add<CameraComponent>(camera, CameraComponent{});

		const Entity sun = AddEntity(world, "Sun", 0xC10D000003ull, Placed(glm::vec3(0.0f)));
		DirectionalLightComponent light;
		light.Intensity = 3.0f;
		world.Add<DirectionalLightComponent>(sun, light);
		const Entity clock = AddEntity(world, "Time Of Day", 0xC10D000004ull, Placed(glm::vec3(0.0f)));
		TimeOfDayComponent timeOfDay;
		timeOfDay.Hours = 17.73f;
		timeOfDay.PathTiltDegrees = 0.0f;
		timeOfDay.SunriseDirectionDegrees = -90.0f;
		timeOfDay.Sun = EntityGuid{ 0xC10D000003ull };
		world.Add<TimeOfDayComponent>(clock, timeOfDay);

		const Entity air = AddEntity(world, "Environment", 0xC10D000005ull, Placed(glm::vec3(0.0f)));
		EnvironmentComponent environment;
		environment.FogDensity = 0.002f;
		world.Add<EnvironmentComponent>(air, environment);

		const Entity layer = AddEntity(world, "Cloud Layer", 0xC10D000006ull, Placed(glm::vec3(0.0f)));
		CloudLayerComponent clouds;
		clouds.Altitude = 3500.0f;
		clouds.Thickness = 250.0f;
		clouds.Coverage = 0.5f;
		clouds.CloudletSize = 300.0f;
		clouds.Softness = 0.35f;
		clouds.Detail = 0.3f;
		clouds.Warp = 0.25f;
		clouds.WindDirectionDegrees = 20.0f;
		clouds.WindSpeed = 8.0f;
		clouds.Brightness = 0.7f;
		clouds.Rim = 1.5f;
		world.Add<CloudLayerComponent>(layer, clouds);

		// The gold-green dusk 3 degrees up, a clear noon at 50, a cool night below the horizon
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
		ColorGrade noon;
		noon.Temperature = 0.05f;
		noon.Saturation = 1.1f;
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Keys = { Key(-8.0f, night), Key(3.0f, goldGreen), Key(50.0f, noon) };
		world.Add<ColorGradingComponent>(air, grading);

		const std::filesystem::path assets = Paths::GetAssetsDir();
		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The cloud layer test scene as the game loads it, its sun turned by its time of day: clouds
	void TestCloudLayerTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The cloud layer test scene can't be loaded; NyxCloudLayerGpuTests --write-scene writes it");
		ApplyTimeOfDay(world);
		FixedStepClock clock;
		clock.SetPaused(true);
		gpu.Renderer.SetWorldInterpolation(&world, &clock);
		Entity layer;
		world.Each<CloudLayerComponent>([&](Entity entity, const CloudLayerComponent&)
			{
				layer = entity;
			});

		gpu.Renderer.SetWorld(&world);
		const uint64_t view = MakeView(gpu, 1280, 720, 0.0f);
		const RgbaImage cloudy = Draw(gpu, view, 3);
		SaveIfAsked(cloudy, "TestScene");
		Require(gpu.Renderer.GetSceneViewCloudLayer(view)->IsDrawn(), "The cloud layer test scene's layer isn't drawn");
		world.Get<CloudLayerComponent>(layer).bClouds = false;
		const RgbaImage clear = Draw(gpu, view, 2);
		SaveIfAsked(clear, "TestSceneClear");
		const Clouds clouds = MeasureClouds(cloudy, clear, 720);
		std::cout << "The cloud layer test scene: " << clouds.Count << " pixels of cloud, " << Text(clouds.Color) << " on average\n";
		Require(clouds.Count > 1280 * 720 / 5, "The cloud layer test scene has too few clouds");
		gpu.Renderer.SetWorldInterpolation(&world, nullptr);
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
		SceneRenderer gpu("NyxCloudLayerGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestUnchangedWithout(gpu);
		TestFromBelow(gpu);
		TestFromAbove(gpu);
		TestCrossing(gpu);
		TestDeterminism(gpu);
		TestGroundUnderTheLayer(gpu);
		TestLightsTheWorld(gpu);
		TestCaptureCost(gpu);
		TestLightShafts(gpu);
		TestGpuTime(gpu, 1280, 720);
		TestGpuTime(gpu, 1920, 1080);
		TestCloudLayerTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxCloudLayerGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxCloudLayerGpuTests: all passed\n";
	return 0;
}

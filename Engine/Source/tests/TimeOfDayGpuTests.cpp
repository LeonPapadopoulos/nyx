// The time of day on a real GPU (TimeOfDayComponent): the time set between frames, as the editor's
// frame and a game's steps set it, turns the sun, and the scene's sky and ground follow: darker
// at sunset than at noon, darkest at midnight. Without the air, a sun the time of day has taken
// below the horizon lights nothing from below: a ceiling under a sun straight below is lit only
// without the time of day.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "TimeOfDay.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 320;
	constexpr uint32_t ViewHeight = 180;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("TimeOfDay-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	int Brightness(const std::array<uint8_t, 3>& rgb)
	{
		return int(rgb[0]) + int(rgb[1]) + int(rgb[2]);
	}

	Entity AddPlane(Registry& world, VulkanRenderer& renderer, const glm::vec3& position, const glm::quat& rotation)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = position, .Rotation = rotation, .Scale = glm::vec3(100.0f, 1.0f, 100.0f) });
		MeshRendererComponent mesh;
		mesh.MeshAsset = renderer.GetPlaneMesh();
		mesh.MaterialAsset = renderer.GetMaterial("Materials/Untextured.nyxmat");
		world.Add<MeshRendererComponent>(entity, mesh);
		return entity;
	}

	// A camera at `position` turned by `rotation`, a sun named by the time of day, on a straight
	// path (overhead at noon, rising at +X)
	struct TimeOfDayScene
	{
		Registry World;
		Entity Sun;
		Entity Clock;

		TimeOfDayScene(const glm::vec3& position, const glm::quat& rotation)
		{
			const Entity camera = World.CreateEntity();
			World.Add<TransformComponent>(camera, TransformComponent{ .Position = position, .Rotation = rotation });
			World.Add<CameraComponent>(camera, CameraComponent{});
			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, TransformComponent{});
			World.Add<GuidComponent>(Sun, GuidComponent{ EntityGuid{ 0x50 } });
			World.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});
			Clock = World.CreateEntity();
			TimeOfDayComponent timeOfDay;
			timeOfDay.PathTiltDegrees = 0.0f;
			timeOfDay.Sun = EntityGuid{ 0x50 };
			World.Add<TimeOfDayComponent>(Clock, timeOfDay);
		}

		// The time set, and the sun turned to it, as a frame of the editor or a game does
		void SetTime(float hours)
		{
			World.Get<TimeOfDayComponent>(Clock).Hours = hours;
			ApplyTimeOfDay(World);
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view)
	{
		gpu.DrawFrames(2);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, Registry& world, ESceneViewSky sky)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		renderer.SetSceneViewSky(view, sky, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// Noon, sunset and midnight under the scene's sky that follows the sun: the sky straight ahead
	// and up, and the ground below it, darker as the day goes
	void TestSkyThroughTheDay(SceneRenderer& gpu)
	{
		// 2 m up, looking along -Z and 20 degrees up: the sky in the top of the picture, the ground
		// in the bottom
		TimeOfDayScene scene(glm::vec3(0.0f, 2.0f, 0.0f), glm::quat(glm::radians(glm::vec3(20.0f, 0.0f, 0.0f))));
		AddPlane(scene.World, gpu.Renderer, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		scene.World.Add<EnvironmentComponent>(scene.World.CreateEntity(), environment);

		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Scene);
		scene.SetTime(12.0f);
		const RgbaImage noon = Draw(gpu, view);
		SaveIfAsked(noon, "Noon");
		scene.SetTime(17.8f);
		const RgbaImage sunset = Draw(gpu, view);
		SaveIfAsked(sunset, "Sunset");
		scene.SetTime(0.0f);
		const RgbaImage midnight = Draw(gpu, view);
		SaveIfAsked(midnight, "Midnight");
		EndView(gpu, view);

		const uint32_t middle = ViewWidth / 2;
		const auto sky = [&](const RgbaImage& image) { return PixelAt(image, middle, 10); };
		const auto ground = [&](const RgbaImage& image) { return PixelAt(image, middle, ViewHeight - 8); };
		Require(Brightness(sky(sunset)) < Brightness(sky(noon)), "The sky at sunset isn't darker than at noon: " + Text(sky(sunset)) + " against " + Text(sky(noon)));
		Require(Brightness(sky(midnight)) * 3 < Brightness(sky(noon)), "The sky at midnight isn't dark: " + Text(sky(midnight)) + " against " + Text(sky(noon)));
		Require(Brightness(ground(sunset)) < Brightness(ground(noon)), "The ground at sunset isn't darker than at noon: " + Text(ground(sunset)) + " against " + Text(ground(noon)));
		Require(Brightness(ground(midnight)) * 4 < Brightness(ground(noon)), "The ground at midnight isn't dark: " + Text(ground(midnight)) + " against " + Text(ground(noon)));
	}

	// No air, a black sky: only the sun lights. A ceiling facing down, under a sun straight below
	// at midnight, is lit by the light as it is, but not by the time of day's sun, which has set.
	void TestNoLightFromBelow(SceneRenderer& gpu)
	{
		TimeOfDayScene scene(glm::vec3(0.0f), glm::quat(glm::radians(glm::vec3(90.0f, 0.0f, 0.0f))));
		AddPlane(scene.World, gpu.Renderer, glm::vec3(0.0f, 5.0f, 0.0f), glm::angleAxis(glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		scene.SetTime(0.0f);
		Require(scene.World.Get<TransformComponent>(scene.Sun).Rotation * glm::vec3(0.0f, 0.0f, -1.0f) == glm::vec3(0.0f, 1.0f, 0.0f) ||
				glm::length(scene.World.Get<TransformComponent>(scene.Sun).Rotation * glm::vec3(0.0f, 0.0f, -1.0f) - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-5f,
			"The sun at midnight doesn't shine straight up");
		const RgbaImage set = Draw(gpu, view);
		SaveIfAsked(set, "CeilingSunSet");
		// The same light, no time of day
		scene.World.Remove<TimeOfDayComponent>(scene.Clock);
		const RgbaImage lit = Draw(gpu, view);
		SaveIfAsked(lit, "CeilingLit");
		EndView(gpu, view);

		const std::array<uint8_t, 3> ceilingSet = PixelAt(set, ViewWidth / 2, ViewHeight / 2);
		const std::array<uint8_t, 3> ceilingLit = PixelAt(lit, ViewWidth / 2, ViewHeight / 2);
		Require(Brightness(ceilingLit) > 300, "The ceiling isn't lit by a light from below: " + Text(ceilingLit));
		Require(Brightness(ceilingSet) <= 3, "The sun that has set lights the ceiling from below: " + Text(ceilingSet));
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		SceneRenderer gpu("NyxTimeOfDayGpuTests");
		TestSkyThroughTheDay(gpu);
		TestNoLightFromBelow(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxTimeOfDayGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxTimeOfDayGpuTests: all passed\n";
	return 0;
}

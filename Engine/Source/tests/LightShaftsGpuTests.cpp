// Light shafts on a real GPU (VulkanLightShafts). A camera looks at a wall 20 m ahead with a square
// window in it, a bright sun low behind the wall, beside the window, under the sky made by the air:
// with the shafts on, the wall beyond the window, on the line from the sun through it, is brighter
// than without, the nearer the window the more; elsewhere on the wall, nothing changes. A sun
// behind the camera, or below the horizon, gives none: the picture is the one without, to the bit,
// and so is one without the environment against an environment with all of it off. The same wall
// and sun from a camera 1.5 km up give the same shafts. Their GPU time while drawn, none while not,
// and the median of 60 frames at 1280 x 720. The light shafts test scene, its sun set by its time
// of day, has its shafts.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there. NyxLightShaftsGpuTests
// --write-scene writes Assets/Scenes/LightShaftsTest.nyxscene.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "LightShafts.h"
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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("LightShafts-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, const glm::vec2& pixel)
	{
		const uint32_t x = std::min(static_cast<uint32_t>(pixel.x), image.Width - 1);
		const uint32_t y = std::min(static_cast<uint32_t>(pixel.y), image.Height - 1);
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	int Brightness(const std::array<uint8_t, 3>& rgb)
	{
		return int(rgb[0]) + int(rgb[1]) + int(rgb[2]);
	}

	// The most any channel of a pixel differs between two pictures
	int MostDifference(const std::array<uint8_t, 3>& a, const std::array<uint8_t, 3>& b)
	{
		return std::max({ std::abs(int(a[0]) - int(b[0])), std::abs(int(a[1]) - int(b[1])), std::abs(int(a[2]) - int(b[2])) });
	}

	TransformComponent Placed(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = rotation, .Scale = scale };
	}

	Entity AddMesh(Registry& world, const TransformComponent& transform, Mesh* mesh, Material* material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, transform);
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = mesh;
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		return entity;
	}

	// A light whose light comes from `towardsSun`: its -Z the other way
	glm::quat LightFrom(const glm::vec3& towardsSun)
	{
		return glm::quatLookAt(-glm::normalize(towardsSun), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	// The shafts alone over the sky made by the air: no fog, no bloom, no ambient occlusion
	EnvironmentComponent ShaftsOnly()
	{
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.bLightShafts = true;
		return environment;
	}

	// The sun's way, 11 degrees up straight ahead: behind the wall, 4 m left of the window
	const glm::vec3 SunAhead = glm::normalize(glm::vec3(0.0f, 0.2f, -1.0f));

	// A camera at `base` looking along -Z at a wall 20 m ahead, its face towards the camera 19.5 m
	// away, filling the picture but for a window 2 m square whose middle is 3 m right of and 4 m
	// above the camera's eye; a sun behind the wall
	struct WindowScene
	{
		Registry World;
		glm::vec3 Base{ 0.0f };
		Entity Camera;
		Entity Sun;
		Entity Environment;
		CameraComponent Lens;

		WindowScene(VulkanRenderer& renderer, const glm::vec3& base)
			: Base(base)
		{
			Mesh* cube = renderer.GetCubeMesh();
			Material* grey = renderer.GetMaterial("Materials/Untextured.nyxmat");
			const auto box = [&](float left, float right, float bottom, float top)
			{
				AddMesh(World, Placed(Base + glm::vec3(0.5f * (left + right), 0.5f * (bottom + top), -20.0f), glm::vec3(right - left, top - bottom, 1.0f)), cube, grey);
			};
			box(-60.0f, 2.0f, -40.0f, 40.0f);
			box(4.0f, 60.0f, -40.0f, 40.0f);
			box(1.9f, 4.1f, 5.0f, 40.0f);
			box(1.9f, 4.1f, -40.0f, 3.0f);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(Base));
			World.Add<CameraComponent>(Camera, Lens);

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), LightFrom(SunAhead)));
			DirectionalLightComponent light;
			light.Intensity = 3.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);

			Environment = World.CreateEntity();
			World.Add<EnvironmentComponent>(Environment, ShaftsOnly());
		}

		EnvironmentComponent& Air()
		{
			return World.Get<EnvironmentComponent>(Environment);
		}

		void SetSun(const glm::vec3& towardsSun)
		{
			World.Get<TransformComponent>(Sun).Rotation = LightFrom(towardsSun);
		}

		// The pixel of a point on the wall's face, `x` right of and `y` above the camera's eye
		glm::vec2 OnWall(float x, float y) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), Base + glm::vec3(x, y, -19.5f));
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, "A point the test looks at is out of the picture");
			return *pixel;
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, uint32_t width, uint32_t height)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		renderer.SetSceneViewSky(view, ESceneViewSky::Scene, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		static_cast<IEditorRenderer&>(gpu.Renderer).DestroySceneView(view);
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// With the shafts and without: the wall beyond the window, on the line from the sun, brighter,
	// the nearer the window the more; off that line, the same within a step
	void RequireShaftsThroughTheWindow(SceneRenderer& gpu, WindowScene& scene, uint64_t view, const std::string& name)
	{
		scene.Air().bLightShafts = true;
		const RgbaImage shafts = Draw(gpu, view);
		SaveIfAsked(shafts, name);
		scene.Air().bLightShafts = false;
		const RgbaImage plain = Draw(gpu, view);
		SaveIfAsked(plain, name + "Off");
		scene.Air().bLightShafts = true;

		const glm::vec2 near = scene.OnWall(5.5f, 4.0f);
		const glm::vec2 far = scene.OnWall(11.0f, 4.0f);
		const int nearGain = Brightness(PixelAt(shafts, near)) - Brightness(PixelAt(plain, near));
		const int farGain = Brightness(PixelAt(shafts, far)) - Brightness(PixelAt(plain, far));
		std::cout << name << ": the wall beside the window " << Text(PixelAt(plain, near)) << " -> " << Text(PixelAt(shafts, near)) << ", further on "
				  << Text(PixelAt(plain, far)) << " -> " << Text(PixelAt(shafts, far)) << "\n";
		Require(nearGain > 30, name + ": the wall beyond the window isn't lit by the shaft: " + Text(PixelAt(plain, near)) + " -> " + Text(PixelAt(shafts, near)));
		Require(farGain > 6 && farGain < nearGain, name + ": further from the window, the shaft isn't dimmer but there: " + std::to_string(farGain) + " against " + std::to_string(nearGain));

		// Left of the sun, below the window's line, and below the sun: their lines to the sun cross
		// no gap
		for (const glm::vec2& point : { glm::vec2(-8.0f, 4.0f), glm::vec2(9.0f, -4.0f), glm::vec2(0.0f, -7.0f), glm::vec2(-14.0f, -8.0f), glm::vec2(1.0f, 4.0f) })
		{
			const glm::vec2 pixel = scene.OnWall(point.x, point.y);
			Require(MostDifference(PixelAt(shafts, pixel), PixelAt(plain, pixel)) <= 1,
				name + ": the wall away from the window changes with the shafts at " + std::to_string(point.x) + ", " + std::to_string(point.y) + ": " + Text(PixelAt(plain, pixel)) + " -> " +
					Text(PixelAt(shafts, pixel)));
		}
	}

	void TestShaftsThroughAWindow(SceneRenderer& gpu)
	{
		WindowScene scene(gpu.Renderer, glm::vec3(0.0f));
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight);
		RequireShaftsThroughTheWindow(gpu, scene, view, "Window");

		// Their GPU time: some while drawn
		gpu.DrawFrames(2);
		const float milliseconds = gpu.Renderer.GetSceneViewGpuTimes(view)->LightShafts;
		Require(milliseconds > 0.0f && milliseconds < 50.0f, "The shafts' GPU time isn't measured: " + std::to_string(milliseconds) + " ms");

		// Off, none
		scene.Air().bLightShafts = false;
		const RgbaImage off = Draw(gpu, view);
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->LightShafts == 0.0f, "Shafts that are off take GPU time");
		// No intensity: the same to the bit
		scene.Air().bLightShafts = true;
		scene.Air().LightShaftsIntensity = 0.0f;
		Require(Draw(gpu, view).Pixels == off.Pixels, "Shafts of no intensity change the picture");
		scene.Air().LightShaftsIntensity = ShaftsOnly().LightShaftsIntensity;

		// A sun behind the camera, below the horizon ahead, or far to the side: none, to the bit, and
		// no GPU time
		for (const auto& [towards, what] : { std::pair{ glm::vec3(0.0f, 0.2f, 1.0f), "behind the camera" }, std::pair{ glm::vec3(0.0f, -0.2f, -1.0f), "below the horizon" },
				 std::pair{ glm::vec3(-1.0f, 0.15f, -0.25f), "far to the side" } })
		{
			scene.SetSun(glm::normalize(towards));
			scene.Air().bLightShafts = false;
			const RgbaImage without = Draw(gpu, view);
			scene.Air().bLightShafts = true;
			const RgbaImage with = Draw(gpu, view);
			SaveIfAsked(with, std::string("Sun ") + what);
			Require(with.Pixels == without.Pixels, std::string("A sun ") + what + " gives shafts");
			Require(gpu.Renderer.GetSceneViewGpuTimes(view)->LightShafts == 0.0f, std::string("The shafts of a sun ") + what + " take GPU time");
		}
		scene.SetSun(SunAhead);

		// Without the environment, as with an environment with all of it off: fog, the sky made by
		// the sun (the plain cube map instead), bloom, ambient occlusion and the shafts
		EnvironmentComponent nothing;
		nothing.bFog = false;
		nothing.SkyMode = Nyx::ESkyMode::CubeMap;
		nothing.bBloom = false;
		nothing.bAmbientOcclusion = false;
		nothing.bLightShafts = false;
		scene.Air() = nothing;
		const RgbaImage allOff = Draw(gpu, view);
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		Require(Draw(gpu, view).Pixels == allOff.Pixels, "Without the environment the picture isn't as with all of it off");
		scene.World.Add<EnvironmentComponent>(scene.Environment, ShaftsOnly());

		// A material's debug view isn't light: no shafts
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::BaseColor);
		gpu.DrawFrames(3);
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->LightShafts == 0.0f && gpu.Renderer.GetSceneViewLightShafts(view)->Extent.width == 0, "A debug view of the materials has shafts");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);

		// The world taken away: its targets let go of, the device kept
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewLightShafts(view)->Extent == vk::Extent2D(ViewWidth / 2, ViewHeight / 2), "The shafts' targets aren't half the view's size");
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(4);
		Require(!gpu.Renderer.IsDeviceLost() && gpu.Renderer.GetSceneViewLightShafts(view)->Extent.width == 0, "A view whose world went away keeps its shafts, or lost the device");
		EndView(gpu, view);
	}

	// The same wall and sun with the camera 1.5 km up, at cloud height: the same shafts
	void TestShaftsHighUp(SceneRenderer& gpu)
	{
		WindowScene scene(gpu.Renderer, glm::vec3(0.0f, 1500.0f, 0.0f));
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight);
		RequireShaftsThroughTheWindow(gpu, scene, view, "HighUp");
		EndView(gpu, view);
	}

	// Their GPU time at 1280 x 720, the median of 60 frames, with the picture pass's
	void TestGpuTime(SceneRenderer& gpu)
	{
		WindowScene scene(gpu.Renderer, glm::vec3(0.0f));
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, 1280, 720);
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
		const float shafts = median(&SceneViewGpuTimes::LightShafts);
		const float picture = median(&SceneViewGpuTimes::Picture);
		scene.Air().bLightShafts = false;
		const float pictureWithout = median(&SceneViewGpuTimes::Picture);
		std::cout << "At 1280 x 720 the light shafts took " << shafts << " ms on the GPU (the median of 60 frames); the picture pass " << picture << " ms with them, "
				  << pictureWithout << " ms without\n";
		Require(shafts > 0.0f && shafts < 1.0f, "The shafts take more than the plan's millisecond for fog and shafts: " + std::to_string(shafts) + " ms");
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/LightShaftsTest.nyxscene";

	Entity AddEntity(Registry& world, const char* name, uint64_t guid, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ guid } });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	void AddBox(Registry& world, const char* name, uint64_t guid, const glm::vec3& position, const glm::vec3& size)
	{
		const Entity entity = AddEntity(world, name, guid, Placed(position, size));
		MeshRendererComponent meshRenderer;
		meshRenderer.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" };
		meshRenderer.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" };
		world.Add<MeshRendererComponent>(entity, meshRenderer);
	}

	// A colonnade against a low sun: a ground, two rows of tall columns under a roof, the camera
	// before them looking through, a hazy air with the shafts on, and a time of day setting the sun
	// behind the columns (sunset at -Z, 12 degrees up at 17:12)
	void WriteScene()
	{
		Registry world;
		AddBox(world, "Ground", 0x5AF7000001ull, glm::vec3(0.0f, -0.5f, -40.0f), glm::vec3(200.0f, 1.0f, 200.0f));
		uint64_t guid = 0x5AF7000100ull;
		for (int row = 0; row < 2; ++row)
		{
			const float z = row == 0 ? -18.0f : -24.0f;
			for (int column = -5; column <= 5; ++column)
			{
				const float x = static_cast<float>(column) * 2.2f + (row == 0 ? 0.0f : 1.1f);
				AddBox(world, "Column", guid++, glm::vec3(x, 6.0f, z), glm::vec3(0.7f, 12.0f, 0.7f));
			}
			AddBox(world, "Roof", guid++, glm::vec3(0.0f, 12.5f, z), glm::vec3(26.0f, 1.0f, 2.5f));
		}

		const Entity camera = AddEntity(world, "Camera", 0x5AF7000002ull, Placed(glm::vec3(0.0f, 1.7f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(8.0f, 0.0f, 0.0f)))));
		world.Add<CameraComponent>(camera, CameraComponent{});

		const Entity sun = AddEntity(world, "Sun", 0x5AF7000003ull, Placed(glm::vec3(0.0f)));
		DirectionalLightComponent light;
		light.Intensity = 3.0f;
		world.Add<DirectionalLightComponent>(sun, light);
		const Entity clock = AddEntity(world, "Time Of Day", 0x5AF7000004ull, Placed(glm::vec3(0.0f)));
		TimeOfDayComponent timeOfDay;
		timeOfDay.Hours = 17.2f;
		timeOfDay.PathTiltDegrees = 0.0f;
		timeOfDay.SunriseDirectionDegrees = -90.0f;
		timeOfDay.Sun = EntityGuid{ 0x5AF7000003ull };
		world.Add<TimeOfDayComponent>(clock, timeOfDay);

		const Entity air = AddEntity(world, "Environment", 0x5AF7000005ull, Placed(glm::vec3(0.0f)));
		EnvironmentComponent environment;
		environment.FogDensity = 0.01f;
		environment.bLightShafts = true;
		world.Add<EnvironmentComponent>(air, environment);

		const std::filesystem::path assets = Paths::GetAssetsDir();
		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The light shafts test scene as the game loads it, its sun turned by its time of day: its sun
	// on the picture, and shafts between the columns
	void TestLightShaftsTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The light shafts test scene can't be loaded; NyxLightShaftsGpuTests --write-scene writes it");
		ApplyTimeOfDay(world);
		Entity air;
		world.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent& environment)
			{
				air = entity;
				Require(environment.bLightShafts, "The light shafts test scene's shafts are off");
			});

		gpu.Renderer.SetWorld(&world);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight);
		const RgbaImage shafts = Draw(gpu, view, 3);
		SaveIfAsked(shafts, "TestScene");
		Require(gpu.Renderer.GetSceneViewLightShafts(view)->Settings.IsDrawn(), "The light shafts test scene's sun gives none");
		world.Get<EnvironmentComponent>(air).bLightShafts = false;
		const RgbaImage plain = Draw(gpu, view, 2);
		SaveIfAsked(plain, "TestSceneOff");
		size_t brighter = 0;
		for (size_t pixel = 0; pixel < shafts.Pixels.size(); pixel += 4)
		{
			brighter += Brightness({ shafts.Pixels[pixel], shafts.Pixels[pixel + 1], shafts.Pixels[pixel + 2] }) >
					Brightness({ plain.Pixels[pixel], plain.Pixels[pixel + 1], plain.Pixels[pixel + 2] }) + 6
				? 1
				: 0;
		}
		std::cout << "The light shafts test scene: " << brighter << " pixels brighter with the shafts\n";
		Require(brighter > shafts.Pixels.size() / 4 / 20, "The light shafts test scene's shafts light only " + std::to_string(brighter) + " pixels");
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
		SceneRenderer gpu("NyxLightShaftsGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestShaftsThroughAWindow(gpu);
		TestShaftsHighUp(gpu);
		TestGpuTime(gpu);
		TestLightShaftsTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxLightShaftsGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxLightShaftsGpuTests: all passed\n";
	return 0;
}

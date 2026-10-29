// Lightning on a real GPU (LightningComponent, VulkanLightning, Lightning.slang), its world's time a
// paused clock's the test sets, so every run draws the same bolt:
//   - off, or between bolts, the picture is the same to the bit as without lightning, and its pass
//     takes no GPU time;
//   - at a flash, the channel is near white where the CPU's path says it is, glowing round it,
//     the bloom spreading it further; the leader alone is dimmer;
//   - behind a wall, nothing of it shows;
//   - its lights light the floor round where it lands, and none with Lights off;
//   - the same time gives the same picture to the bit, another strike another;
//   - a column draws its braid, and boils: another reform frame, another picture;
//   - a click on its icon selects it;
//   - its GPU and CPU time at 1920 x 1080, a strike's flash and a column, the medians of 60 frames;
//   - under the air's sky with volumetric clouds: the clouds round a column glow, those far from it
//     are the same to the bit; once it goes, the very next frame is the picture without it to the
//     bit, marching one pixel of each 4 x 4 a frame (nothing carried); with it, a still picture in
//     4 x 4 is the one every pixel marched gives, to the bit; the bolt's top hidden in the cloud;
//     the clouds' lightning pass's GPU time at 1920 x 1080, beside the march's in each mode.
//
// --look renders a column and a strike framed as the references are (Lightning-Look-*), to compare
// side by side. --clouds renders the clouds' tests alone and saves their pictures.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "FixedStepClock.h"
#include "ImageFile.h"
#include "Lightning.h"
#include "LightningComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "TransformComponent.h"
#include "VolumetricCloudsComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Lightning-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	int BrightnessAt(const RgbaImage& image, const glm::ivec2& pixel)
	{
		const size_t offset = (static_cast<size_t>(pixel.y) * image.Width + pixel.x) * 4;
		return int(image.Pixels[offset]) + int(image.Pixels[offset + 1]) + int(image.Pixels[offset + 2]);
	}

	// The brightest pixel within `reach` of `pixel`
	int BrightestNear(const RgbaImage& image, const glm::ivec2& pixel, int reach)
	{
		int brightest = 0;
		for (int y = std::max(pixel.y - reach, 0); y <= std::min(pixel.y + reach, static_cast<int>(image.Height) - 1); ++y)
		{
			for (int x = std::max(pixel.x - reach, 0); x <= std::min(pixel.x + reach, static_cast<int>(image.Width) - 1); ++x)
			{
				brightest = std::max(brightest, BrightnessAt(image, glm::ivec2(x, y)));
			}
		}
		return brightest;
	}

	size_t CountBrighterThan(const RgbaImage& image, int threshold)
	{
		size_t count = 0;
		for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
		{
			count += int(image.Pixels[pixel]) + int(image.Pixels[pixel + 1]) + int(image.Pixels[pixel + 2]) > threshold ? 1 : 0;
		}
		return count;
	}

	size_t DifferentPixels(const RgbaImage& a, const RgbaImage& b)
	{
		size_t different = 0;
		for (size_t pixel = 0; pixel < a.Pixels.size(); pixel += 4)
		{
			different += std::memcmp(&a.Pixels[pixel], &b.Pixels[pixel], 3) != 0 ? 1 : 0;
		}
		return different;
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

	// A strike 80 m ahead, landing on the floor, starting 70 m up, once, at the world's time 1 s
	LightningComponent TestStrike()
	{
		LightningComponent lightning;
		lightning.StartTime = 1.0f;
		lightning.bRepeat = false;
		lightning.Height = 70.0f;
		lightning.TopSpread = 5.0f;
		lightning.Seed = 3;
		return lightning;
	}

	// A camera looking along -z over a dark floor, under a black sky and a sun that gives no light,
	// the world's time a paused clock's; a strike 80 m ahead
	struct LightningScene
	{
		Registry World;
		LiveMaterialFile BlackFile{ "Materials/LightningTestBlack.nyxmat" };
		LiveMaterialFile GreyFile{ "Materials/LightningTestGrey.nyxmat" };
		Material* Black = nullptr;
		Material* Grey = nullptr;
		Entity Floor;
		Entity Camera;
		Entity Air;
		Entity Bolt;
		CameraComponent Lens;
		FixedStepClock Clock;
		glm::uvec2 Size{ ViewWidth, ViewHeight };

		explicit LightningScene(VulkanRenderer& renderer)
		{
			BlackFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\n");
			GreyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.5 0.5 0.5\nMetallic = 0\nRoughness = 0.9\n");
			Black = renderer.GetMaterial(BlackFile.Path);
			Grey = renderer.GetMaterial(GreyFile.Path);
			Floor = AddMesh(World, Placed(glm::vec3(0.0f, -1.5f, 0.0f), glm::vec3(1000.0f, 1.0f, 1000.0f)), renderer.GetPlaneMesh(), Black);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(8.0f, 0.0f, 0.0f)))));
			World.Add<CameraComponent>(Camera, Lens);

			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);

			Air = World.CreateEntity();
			EnvironmentComponent environment;
			environment.bFog = false;
			environment.bBloom = false;
			environment.bAmbientOcclusion = false;
			environment.bLightShafts = false;
			World.Add<EnvironmentComponent>(Air, environment);

			Bolt = World.CreateEntity();
			World.Add<TransformComponent>(Bolt, Placed(glm::vec3(0.0f, -1.5f, -80.0f)));
			World.Add<LightningComponent>(Bolt, TestStrike());
			SetTime(0.5);
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

		LightningComponent& Lightning()
		{
			return World.Get<LightningComponent>(Bolt);
		}

		// The time of the strike's first flash, just after it
		double FlashTime()
		{
			const LightningComponent clamped = GetClampedLightning(Lightning());
			return static_cast<double>(clamped.StartTime + clamped.LeaderTime) + 0.002;
		}

		// What the CPU says the bolt is at the clock's time
		LightningFrame Evaluate()
		{
			LightningFrame frame;
			EvaluateLightning(Lightning(), World.Get<TransformComponent>(Bolt).Position, static_cast<float>(Clock.GetWorldTimeSeconds()), frame);
			return frame;
		}

		std::optional<glm::ivec2> Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(Size), point);
			if (!pixel || pixel->x < 0.0f || pixel->y < 0.0f || pixel->x >= static_cast<float>(Size.x) || pixel->y >= static_cast<float>(Size.y))
			{
				return std::nullopt;
			}
			return glm::ivec2(glm::floor(*pixel));
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, LightningScene& scene, uint32_t width = ViewWidth, uint32_t height = ViewHeight)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		scene.Size = glm::uvec2(width, height);
		renderer.SetWorld(&scene.World);
		renderer.SetWorldInterpolation(&scene.World, &scene.Clock);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		renderer.SetSceneViewSky(view, ESceneViewSky::Black, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, LightningScene& scene, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorldInterpolation(&scene.World, nullptr);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// Off, between bolts, or with none: the same picture to the bit, and no GPU time
	void TestUnchangedWithout(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Floor).MaterialAsset = scene.Grey;
		const uint64_t view = MakeView(gpu, scene);
		const double flashTime = scene.FlashTime();
		scene.World.Remove<LightningComponent>(scene.Bolt);
		scene.SetTime(flashTime);
		const RgbaImage without = Draw(gpu, view);
		scene.World.Add<LightningComponent>(scene.Bolt, TestStrike());
		scene.Lightning().bEnabled = false;
		const RgbaImage off = Draw(gpu, view);
		Require(DifferentPixels(without, off) == 0, "Lightning that is off changes the picture");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->Lightning == 0.0f, "Lightning that is off takes GPU time");
		scene.Lightning().bEnabled = true;
		scene.SetTime(0.5);
		const RgbaImage before = Draw(gpu, view);
		Require(DifferentPixels(without, before) == 0, "Lightning between bolts changes the picture");
		scene.SetTime(scene.FlashTime());
		const RgbaImage flash = Draw(gpu, view);
		SaveIfAsked(flash, "Flash");
		Require(DifferentPixels(without, flash) > 100, "A flash changes nothing");
		EndView(gpu, scene, view);
	}

	// At a flash: the channel near white where the CPU says it is, glowing round it, the bloom
	// spreading it; the leader dimmer; behind a wall nothing
	void TestBolt(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene);
		scene.SetTime(scene.FlashTime());
		const RgbaImage flash = Draw(gpu, view);
		const LightningFrame frame = scene.Evaluate();
		Require(frame.Channel.has_value(), "At the flash the CPU has a channel");
		const LightningPath& channel = frame.Shape.Paths[*frame.Channel];
		int found = 0;
		int looked = 0;
		for (uint32_t index = channel.PointCount / 2; index < channel.PointCount; index += 8)
		{
			const std::optional<glm::ivec2> pixel = scene.Pixel(frame.Shape.Points[channel.FirstPoint + index].Position);
			if (!pixel)
			{
				continue;
			}
			++looked;
			found += BrightestNear(flash, *pixel, 1) >= 600 ? 1 : 0;
		}
		Require(looked >= 4 && found * 10 >= looked * 9, "The channel isn't near white where its path is: " + std::to_string(found) + " of " + std::to_string(looked));

		// Its glow: a few pixels beside the channel, lit; far from it, black
		const glm::vec3 middle = frame.Shape.Points[channel.FirstPoint + channel.PointCount * 3 / 4].Position;
		const std::optional<glm::ivec2> at = scene.Pixel(middle);
		Require(at.has_value(), "The channel's lower part is in the picture");
		Require(BrightnessAt(flash, glm::ivec2(40, 40)) == 0, "Far from the bolt, the black sky stays black");

		// The bloom spreads it
		scene.World.Get<EnvironmentComponent>(scene.Air).bBloom = true;
		const RgbaImage bloomed = Draw(gpu, view);
		SaveIfAsked(bloomed, "Bloom");
		const glm::ivec2 beside(std::min(at->x + 14, static_cast<int>(ViewWidth) - 1), at->y);
		Require(BrightnessAt(bloomed, beside) > BrightnessAt(flash, beside) + 10, "The bloom doesn't spread the bolt's light");
		scene.World.Get<EnvironmentComponent>(scene.Air).bBloom = false;

		// The leader alone, dimmer and shorter
		scene.SetTime(static_cast<double>(scene.Lightning().StartTime) + scene.Lightning().LeaderTime * 0.5);
		const RgbaImage leader = Draw(gpu, view);
		SaveIfAsked(leader, "Leader");
		Require(CountBrighterThan(leader, 600) < CountBrighterThan(flash, 600) / 2, "The leader isn't dimmer than the flash");
		Require(CountBrighterThan(leader, 30) > 0, "The leader shows nothing");

		// Behind a wall, nothing of it (its lights off, so the wall's back isn't lit either)
		scene.SetTime(scene.FlashTime());
		scene.Lightning().bLights = false;
		const Entity wall = AddMesh(scene.World, Placed(glm::vec3(0.0f, 0.0f, -20.0f), glm::vec3(1000.0f, 1.0f, 1000.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
			gpu.Renderer.GetPlaneMesh(), scene.Black);
		const RgbaImage hidden = Draw(gpu, view);
		scene.World.Remove<LightningComponent>(scene.Bolt);
		const RgbaImage wallAlone = Draw(gpu, view);
		Require(DifferentPixels(hidden, wallAlone) == 0, "A bolt behind a wall shows through it");
		scene.World.DestroyEntity(wall);
		EndView(gpu, scene, view);
	}

	// Its lights light the floor where it lands, none with Lights off
	void TestLights(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Floor).MaterialAsset = scene.Grey;
		scene.World.Get<TransformComponent>(scene.Bolt).Position = glm::vec3(0.0f, -1.5f, -30.0f);
		const uint64_t view = MakeView(gpu, scene);
		scene.SetTime(scene.FlashTime());
		const LightningFrame frame = scene.Evaluate();
		const std::optional<glm::ivec2> floor = scene.Pixel(frame.Ground + glm::vec3(4.0f, 0.0f, 6.0f));
		Require(floor.has_value(), "The floor by where it lands is in the picture");
		const RgbaImage lit = Draw(gpu, view);
		SaveIfAsked(lit, "LitFloor");
		scene.Lightning().bLights = false;
		const RgbaImage dark = Draw(gpu, view);
		Require(BrightnessAt(lit, *floor) > BrightnessAt(dark, *floor) + 60, "The bolt's lights don't light the floor: " + std::to_string(BrightnessAt(lit, *floor)) + " against " +
			std::to_string(BrightnessAt(dark, *floor)));
		Require(BrightnessAt(dark, *floor) < 10, "Without its lights, the floor stays dark");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->Lightning == 1 && stats->LightningCulled == 0, "The bolt isn't counted as drawn");
		EndView(gpu, scene, view);
	}

	// The same time, the same picture to the bit; the next strike another; a column boils
	void TestDeterminismAndColumn(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene);
		scene.SetTime(scene.FlashTime());
		const RgbaImage first = Draw(gpu, view);
		const RgbaImage again = Draw(gpu, view, 3);
		Require(DifferentPixels(first, again) == 0, "The same time doesn't give the same picture");
		scene.Lightning().Seed = 4;
		const RgbaImage other = Draw(gpu, view);
		Require(DifferentPixels(first, other) > 100, "Another seed gives the same strike");

		// A column on the floor 60 m ahead: its braid, a new shape the next reform frame
		LightningComponent& column = scene.Lightning();
		column.Mode = ELightningMode::Sustained;
		column.Duration = 0.0f;
		column.Height = 120.0f;
		column.Brightness = 25.0f;
		column.StrayBoltRate = 0.0f;
		scene.World.Get<TransformComponent>(scene.Bolt).Position = glm::vec3(0.0f, -1.5f, -60.0f);
		scene.SetTime(2.0 + 0.01);
		const RgbaImage braid = Draw(gpu, view);
		SaveIfAsked(braid, "Column");
		Require(CountBrighterThan(braid, 600) > 100, "The column draws little");
		scene.SetTime(2.0 + 0.04);
		Require(DifferentPixels(braid, Draw(gpu, view)) == 0, "Within a reform frame the column changes");
		scene.SetTime(2.0 + 0.1);
		Require(DifferentPixels(braid, Draw(gpu, view)) > 100, "The next reform frame looks the same");
		EndView(gpu, scene, view);
	}

	// The references' framing, to compare the look with them: under the air's sky at dusk with
	// volumetric clouds over a dark ground, a standing column 70 m away, the camera low on the ground
	// looking up at it (the column in the middle, the ground at about four fifths of the picture's
	// height), its reform frames one after another; and a strike 400 m off at its flash, framed as a
	// strike over a town is. Into NYX_SCENE_TEST_OUTPUT as Lightning-Look-*.png.
	void RenderLook(SceneRenderer& gpu)
	{
		Registry world;
		LiveMaterialFile groundFile("Materials/LightningTestLookGround.nyxmat");
		groundFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.07 0.065 0.06\nMetallic = 0\nRoughness = 1\n");
		AddMesh(world, Placed(glm::vec3(0.0f), glm::vec3(8000.0f, 1.0f, 8000.0f)), gpu.Renderer.GetPlaneMesh(), gpu.Renderer.GetMaterial(groundFile.Path));

		CameraComponent lens;
		lens.FovYDegrees = 50.0f;
		lens.FarPlane = 3000.0f;
		const Entity camera = world.CreateEntity();
		world.Add<CameraComponent>(camera, lens);

		const Entity sun = world.CreateEntity();
		const glm::vec3 towardsSun(0.3f, std::sin(glm::radians(2.0f)), 0.95f);
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quatLookAt(-glm::normalize(towardsSun), glm::vec3(0.0f, 1.0f, 0.0f))));
		DirectionalLightComponent light;
		light.Intensity = 1.0f;
		light.bCastShadows = false;
		world.Add<DirectionalLightComponent>(sun, light);

		const Entity air = world.CreateEntity();
		EnvironmentComponent environment;
		environment.FogDensity = 0.002f;
		environment.BloomThreshold = 1.0f;
		environment.BloomIntensity = 0.35f;
		environment.bAmbientOcclusion = false;
		environment.bLightShafts = false;
		world.Add<EnvironmentComponent>(air, environment);
		VolumetricCloudsComponent clouds;
		clouds.Coverage = 0.8f;
		clouds.BottomAltitude = 300.0f;
		clouds.TopAltitude = 1500.0f;
		clouds.WindSpeed = 0.0f;
		clouds.Update = EVolumetricCloudsUpdate::EveryPixel;
		world.Add<VolumetricCloudsComponent>(air, clouds);

		const Entity bolt = world.CreateEntity();
		world.Add<TransformComponent>(bolt, Placed(glm::vec3(0.0f, 0.0f, -70.0f)));
		LightningComponent column;
		column.Mode = ELightningMode::Sustained;
		column.Duration = 0.0f;
		column.bRepeat = false;
		column.Height = 600.0f;
		column.StrayBoltRate = 0.0f;
		world.Add<LightningComponent>(bolt, column);

		FixedStepClock clock;
		const auto setTime = [&](double seconds)
		{
			clock = FixedStepClock{};
			clock.SetPaused(true);
			clock.SetStepSeconds(seconds);
			clock.StepOnce();
			clock.Advance(0.0);
		};
		setTime(1.0);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		renderer.SetWorldInterpolation(&world, &clock);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, 600, 284);
		renderer.SetSceneViewSky(view, ESceneViewSky::Scene, 0.0f);
		gpu.DrawUntilShadersCompiled();

		// The column: the ground at four fifths of the height, 1.5 m up
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(15.0f), glm::vec3(1.0f, 0.0f, 0.0f))));
		for (int frame = 0; frame < 12; ++frame)
		{
			setTime(1.0 + (static_cast<double>(frame) + 0.5) / 12.0);
			char name[32];
			std::snprintf(name, sizeof(name), "Look-Column-%02d", frame);
			SaveIfAsked(Draw(gpu, view, 3), name);
		}

		// The strike, 400 m off, at its flash, the camera a little up, looking up at it
		LightningComponent& strike = world.Get<LightningComponent>(bolt);
		strike = LightningComponent{};
		strike.StartTime = 2.0f;
		strike.bRepeat = false;
		strike.Height = 500.0f;
		strike.TopSpread = 40.0f;
		strike.Seed = 6;
		world.Get<TransformComponent>(bolt).Position = glm::vec3(0.0f, 0.0f, -400.0f);
		renderer.SetSceneViewSize(view, 540, 413);
		world.Get<TransformComponent>(camera) = Placed(glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(12.7f), glm::vec3(1.0f, 0.0f, 0.0f)));
		setTime(2.0 + GetClampedLightning(strike).LeaderTime + 0.004);
		SaveIfAsked(Draw(gpu, view, 4), "Look-Strike");

		renderer.DestroySceneView(view);
		renderer.SetWorldInterpolation(&world, nullptr);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// In the editor's scene views, a click on a lightning entity's icon selects it
	void TestEditorIcon(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		scene.World.Get<TransformComponent>(scene.Bolt).Position = glm::vec3(1.0f, 0.0f, -4.0f);
		const uint64_t view = MakeView(gpu, scene);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetSceneViewShowEditorOverlays(view, true);
		Draw(gpu, view);
		const std::optional<glm::ivec2> icon = scene.Pixel(glm::vec3(1.0f, 0.0f, -4.0f));
		Require(icon.has_value(), "The icon is in the picture");
		renderer.RequestPick(view, icon->x, icon->y);
		Draw(gpu, view, 3);
		const IEditorRenderer::PickResult pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && pick.HitEntity == scene.Bolt, "A click on a lightning entity's icon doesn't pick it");
		EndView(gpu, scene, view);
	}

	float Luminance(const RgbaImage& image, const glm::ivec2& pixel)
	{
		const size_t offset = (static_cast<size_t>(pixel.y) * image.Width + pixel.x) * 4;
		return 0.2126f * image.Pixels[offset] + 0.7152f * image.Pixels[offset + 1] + 0.0722f * image.Pixels[offset + 2];
	}

	// The mean luminance of a square of pixels round `pixel`
	float MeanLuminance(const RgbaImage& image, const glm::ivec2& pixel, int reach)
	{
		float sum = 0.0f;
		int count = 0;
		for (int y = std::max(pixel.y - reach, 0); y <= std::min(pixel.y + reach, static_cast<int>(image.Height) - 1); ++y)
		{
			for (int x = std::max(pixel.x - reach, 0); x <= std::min(pixel.x + reach, static_cast<int>(image.Width) - 1); ++x)
			{
				sum += Luminance(image, glm::ivec2(x, y));
				++count;
			}
		}
		return sum / static_cast<float>(std::max(count, 1));
	}

	// Frames after which every pixel of the clouds was marched again since a change: 16, and the one
	// that changed
	constexpr int SettledFrames = 18;

	// The air's sky at dusk with an overcast of volumetric clouds, their base at 400 m, still (no
	// wind), the camera on the ground looking up at them; a standing column 2.5 km ahead, its top in
	// them; the world's time a paused clock's
	struct CloudyScene
	{
		Registry World;
		FixedStepClock Clock;
		Entity Camera;
		Entity Air;
		Entity Band;
		Entity Bolt;
		CameraComponent Lens;
		glm::uvec2 Size{ ViewWidth, ViewHeight };

		CloudyScene()
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(14.0f), glm::vec3(1.0f, 0.0f, 0.0f))));
			World.Add<CameraComponent>(Camera, Lens);

			const Entity sun = World.CreateEntity();
			const glm::vec3 towardsSun(0.0f, std::sin(glm::radians(6.0f)), std::cos(glm::radians(6.0f)));
			World.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quatLookAt(-towardsSun, glm::vec3(0.0f, 1.0f, 0.0f))));
			DirectionalLightComponent light;
			light.Intensity = 3.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);

			Air = World.CreateEntity();
			EnvironmentComponent environment;
			environment.bFog = false;
			environment.bBloom = false;
			environment.bAmbientOcclusion = false;
			environment.bLightShafts = false;
			World.Add<EnvironmentComponent>(Air, environment);

			Band = World.CreateEntity();
			VolumetricCloudsComponent clouds;
			clouds.Coverage = 0.8f;
			clouds.Type = 0.4f;
			clouds.BottomAltitude = 400.0f;
			clouds.TopAltitude = 1400.0f;
			clouds.WindSpeed = 0.0f;
			World.Add<VolumetricCloudsComponent>(Band, clouds);

			Bolt = World.CreateEntity();
			World.Add<TransformComponent>(Bolt, Placed(glm::vec3(0.0f, 0.0f, -2500.0f)));
			LightningComponent column;
			column.Mode = ELightningMode::Sustained;
			column.Duration = 0.0f;
			column.bRepeat = false;
			column.StartTime = 0.0f;
			column.Height = 1300.0f;
			column.SideArcRate = 0.0f;
			column.StrayBoltRate = 0.0f;
			column.Width = 3.0f;
			World.Add<LightningComponent>(Bolt, column);

			Clock.SetPaused(true);
			Clock.SetStepSeconds(10.0);
			Clock.StepOnce();
			Require(Clock.Advance(0.0) == 1, "The test's clock can't be set");
		}

		VolumetricCloudsComponent& Clouds()
		{
			return World.Get<VolumetricCloudsComponent>(Band);
		}

		LightningComponent& Lightning()
		{
			return World.Get<LightningComponent>(Bolt);
		}

		glm::ivec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(Size), point);
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < static_cast<float>(Size.x) && pixel->y < static_cast<float>(Size.y),
				"A point the test looks at is out of the picture");
			return glm::ivec2(glm::floor(*pixel));
		}
	};

	uint64_t MakeCloudyView(SceneRenderer& gpu, CloudyScene& scene, uint32_t width = ViewWidth, uint32_t height = ViewHeight)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		scene.Size = glm::uvec2(width, height);
		renderer.SetWorld(&scene.World);
		renderer.SetWorldInterpolation(&scene.World, &scene.Clock);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		renderer.SetSceneViewSky(view, ESceneViewSky::Scene, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndCloudyView(SceneRenderer& gpu, CloudyScene& scene, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorldInterpolation(&scene.World, nullptr);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// The clouds round a column glow, far from it they are the same to the bit; the frame after it
	// goes is the picture without it, to the bit, in 4 x 4; with it, 4 x 4 is every pixel marched, to
	// the bit; the bolt's top hidden in the cloud
	void TestCloudsLit(SceneRenderer& gpu)
	{
		CloudyScene scene;
		const uint64_t view = MakeCloudyView(gpu, scene);
		scene.Clouds().Update = EVolumetricCloudsUpdate::Blocks4x4;
		scene.Lightning().bEnabled = false;
		const RgbaImage without = Draw(gpu, view, SettledFrames);
		SaveIfAsked(without, "CloudsWithout");
		scene.Lightning().bEnabled = true;
		const RgbaImage lit = Draw(gpu, view, SettledFrames);
		SaveIfAsked(lit, "CloudsLit");

		// Round it the cloud's base glows; far from it, the same to the bit
		const glm::ivec2 base = scene.Pixel(glm::vec3(120.0f, 480.0f, -2500.0f));
		const float litBase = MeanLuminance(lit, base, 4);
		const float darkBase = MeanLuminance(without, base, 4);
		std::cout << "The cloud's base by the column: " << darkBase << " without it, " << litBase << " lit by it\n";
		Require(litBase > darkBase + 15.0f, "The clouds by the column don't glow");
		Require(BrightnessAt(lit, glm::ivec2(3, 3)) == BrightnessAt(without, glm::ivec2(3, 3)), "The clouds far from the column change");
		const SceneViewGpuTimes* times = gpu.Renderer.GetSceneViewGpuTimes(view);
		Require(times && times->CloudLightning > 0.0f, "The clouds' lightning pass takes no GPU time");

		// The column below the cloud's base, 2.5 km off, past the camera's far plane: shown against the
		// sky; its top in the cloud hidden, the cloud's glow there instead
		const glm::ivec2 low = scene.Pixel(glm::vec3(0.0f, 200.0f, -2500.0f));
		Require(BrightestNear(lit, low, 3) >= 600, "The column below the clouds, past the far plane, doesn't show");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->Lightning == 1 && stats->LightningCulled == 0, "The column past the far plane is culled");

		// The very next frame after it goes: the picture without it, nothing of it carried
		scene.Lightning().bEnabled = false;
		const RgbaImage after = Draw(gpu, view, 1);
		Require(DifferentPixels(after, without) == 0, "The frame after the column went differs from the picture without it in " +
			std::to_string(DifferentPixels(after, without)) + " pixels: its light was carried");
		// Its times are read a frame late, after the frame's fence
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->CloudLightning == 0.0f, "Without a bolt, the clouds' lightning pass still takes GPU time");

		// With it, a still picture marched one pixel of each 4 x 4 a frame is every pixel marched's
		scene.Lightning().bEnabled = true;
		const RgbaImage blocks = Draw(gpu, view, SettledFrames);
		scene.Clouds().Update = EVolumetricCloudsUpdate::EveryPixel;
		const RgbaImage every = Draw(gpu, view, 3);
		Require(DifferentPixels(blocks, every) == 0, "Lit by a bolt, the clouds in 4 x 4 differ from every pixel marched in " + std::to_string(DifferentPixels(blocks, every)) + " pixels");
		EndCloudyView(gpu, scene, view);
	}

	// The clouds' lightning pass's GPU time at 1920 x 1080, and the march's in each mode beside it
	void TestCloudLightningTime(SceneRenderer& gpu)
	{
		CloudyScene scene;
		const uint64_t view = MakeCloudyView(gpu, scene, 1920, 1080);
		const auto median = [&](float SceneViewGpuTimes::* pass)
		{
			std::vector<float> times;
			gpu.DrawFrames(SettledFrames);
			for (int frame = 0; frame < 60; ++frame)
			{
				gpu.DrawFrames(1);
				times.push_back(gpu.Renderer.GetSceneViewGpuTimes(view)->*pass);
			}
			std::sort(times.begin(), times.end());
			return times[times.size() / 2];
		};
		const float lightning = median(&SceneViewGpuTimes::CloudLightning);
		SaveIfAsked(Draw(gpu, view, 1), "CloudsLitFull");
		const SceneViewVolumetricClouds* clouds = gpu.Renderer.GetSceneViewVolumetricClouds(view);
		Require(clouds && !clouds->LightningArea.IsEmpty(), "The column's light shows on no part of the clouds' picture");
		const LightningRect area = clouds->LightningArea;
		const float part = static_cast<float>((area.X1 - area.X0) * (area.Y1 - area.Y0)) / static_cast<float>(clouds->Extent.width * clouds->Extent.height);
		std::cout << "At 1920 x 1080 the clouds' lightning pass took " << lightning << " ms on the GPU over " << static_cast<int>(part * 100.0f + 0.5f)
				  << " % of their picture (" << area.X1 - area.X0 << " x " << area.Y1 - area.Y0 << " of " << clouds->Extent.width << " x " << clouds->Extent.height << ")\n";
		for (const EVolumetricCloudsUpdate update : { EVolumetricCloudsUpdate::Blocks4x4, EVolumetricCloudsUpdate::EveryPixel })
		{
			scene.Clouds().Update = update;
			const float march = median(&SceneViewGpuTimes::VolumetricClouds);
			std::cout << "  their march " << (update == EVolumetricCloudsUpdate::EveryPixel ? "marching every pixel" : "in 4 x 4") << ": " << march << " ms\n";
		}
		Require(lightning > 0.0f && lightning < 3.0f, "The clouds' lightning pass takes 3 milliseconds or more");
		EndCloudyView(gpu, scene, view);
	}

	// The pass's GPU time at 1920 x 1080, held at a strike's flash and on a column, and the CPU's time
	// to work out the bolt and its strips
	void TestGpuTime(SceneRenderer& gpu)
	{
		LightningScene scene(gpu.Renderer);
		scene.World.Get<EnvironmentComponent>(scene.Air).bBloom = true;
		const uint64_t view = MakeView(gpu, scene, 1920, 1080);
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
		const auto cpu = [&]()
		{
			const LightningPixelScale pixels = GetLightningPixelScale(glm::mat4(1.0f), 1080);
			std::vector<float> times;
			std::vector<LightningVertex> vertices;
			for (int run = 0; run < 60; ++run)
			{
				const auto start = std::chrono::steady_clock::now();
				const LightningFrame frame = scene.Evaluate();
				vertices.clear();
				BuildLightningStrips(frame.Shape, MakeLightningLook(scene.Lightning()), glm::vec3(0.0f), pixels, vertices);
				times.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count());
			}
			std::sort(times.begin(), times.end());
			return std::pair<float, size_t>(times[times.size() / 2], vertices.size());
		};

		scene.SetTime(scene.FlashTime());
		const float strike = median(&SceneViewGpuTimes::Lightning);
		const auto [strikeCpu, strikeVertices] = cpu();
		LightningComponent& column = scene.Lightning();
		column.Mode = ELightningMode::Sustained;
		column.Duration = 0.0f;
		column.Height = 400.0f;
		scene.World.Get<TransformComponent>(scene.Bolt).Position = glm::vec3(0.0f, -1.5f, -60.0f);
		scene.SetTime(2.0);
		const float standing = median(&SceneViewGpuTimes::Lightning);
		SaveIfAsked(Draw(gpu, view, 1), "ColumnFull");
		const auto [columnCpu, columnVertices] = cpu();
		std::cout << "At 1920 x 1080, the lightning pass took " << strike << " ms on the GPU at a strike's flash (" << strikeVertices << " vertices, " << strikeCpu
				  << " ms on the CPU to work out), " << standing << " ms on a column (" << columnVertices << " vertices, " << columnCpu << " ms on the CPU)\n";
		Require(strike > 0.0f && strike < 1.0f && standing < 1.0f, "The lightning pass takes a millisecond or more");
		EndView(gpu, scene, view);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		SceneRenderer gpu("NyxLightningGpuTests", MsaaSamplesFromArguments(argc, argv));
		if (argc > 1 && std::strcmp(argv[1], "--look") == 0)
		{
			RenderLook(gpu);
			return 0;
		}
		if (argc > 1 && std::strcmp(argv[1], "--clouds") == 0)
		{
			TestCloudsLit(gpu);
			TestCloudLightningTime(gpu);
			return 0;
		}
		TestUnchangedWithout(gpu);
		TestBolt(gpu);
		TestLights(gpu);
		TestDeterminismAndColumn(gpu);
		TestEditorIcon(gpu);
		TestGpuTime(gpu);
		TestCloudsLit(gpu);
		TestCloudLightningTime(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxLightningGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxLightningGpuTests: all passed\n";
	return 0;
}

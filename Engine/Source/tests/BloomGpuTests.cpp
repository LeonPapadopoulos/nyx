// Bloom on a real GPU (VulkanBloom). An emissive cube and an emissive decal on a black wall, under a
// black sky: with bloom, their light spreads into the black round them, and far from them the
// wall stays black; without, nothing spreads. A threshold above their light, or an exposure that
// takes them under it, changes nothing, to the bit. A wider radius spreads further. The view's
// chain has as many levels as its size allows; a second, smaller view has its own. The debug view
// shows the bloom alone. Without the environment, or with its bloom off, the picture is the same. A
// view whose world goes away lets its chain go and keeps drawing.
// The scene looks as its reference picture does, and the atmosphere test scene's glowing cube and
// decal bloom.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "AssetResolver.h"
#include "Bloom.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Bloom-" + name + ".png");
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

	// Bloom alone: no fog, the sky as it is, no ambient occlusion
	EnvironmentComponent BloomOnly()
	{
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.SkyMode = ESkyMode::CubeMap;
		environment.bAmbientOcclusion = false;
		return environment;
	}

	// A camera at the origin looking along -Z at a black wall 10 m away, an emissive cube 6 m away
	// on the left, an emissive ring on the wall on the right, a sun that gives no light, a black sky
	struct GlowScene
	{
		Registry World;
		LiveMaterialFile BlackFile{ "Materials/BloomTestBlack.nyxmat" };
		Entity Wall;
		Entity Cube;
		Entity Decal;
		Entity Camera;
		Entity Environment;
		CameraComponent Lens;

		explicit GlowScene(VulkanRenderer& renderer)
		{
			BlackFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\n");
			Wall = AddMesh(World, Placed(glm::vec3(0.0f, 0.0f, -10.0f), glm::vec3(60.0f, 1.0f, 60.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
				renderer.GetPlaneMesh(), renderer.GetMaterial(BlackFile.Path));
			World.Add<GuidComponent>(Wall, GuidComponent{ EntityGuid{ 0xB100 } });
			Cube = AddMesh(World, Placed(glm::vec3(-1.6f, 0.0f, -6.0f), glm::vec3(0.6f), glm::quat(glm::radians(glm::vec3(20.0f, 35.0f, 0.0f)))), renderer.GetCubeMesh(),
				renderer.GetMaterial("Materials/Emissive.nyxmat"));

			Decal = World.CreateEntity();
			SurfaceDecalComponent decal;
			decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0xB100 }, .LocalPosition = glm::vec3(0.0f), .LocalNormal = glm::vec3(0.0f, 1.0f, 0.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
			decal.Anchor.LocalPosition = glm::vec3(3.0f / 60.0f, 0.0f, 0.0f);
			decal.Size = glm::vec2(2.0f, 2.0f);
			decal.MaterialAsset = renderer.GetMaterial("Materials/MaskedDecal.nyxmat");
			decal.Color = glm::vec3(0.3f, 0.8f, 1.0f);
			decal.Intensity = 6.0f;
			World.Add<SurfaceDecalComponent>(Decal, decal);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f)));
			World.Add<CameraComponent>(Camera, Lens);

			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);

			Environment = World.CreateEntity();
			World.Add<EnvironmentComponent>(Environment, BloomOnly());
		}

		EnvironmentComponent& Air()
		{
			return World.Get<EnvironmentComponent>(Environment);
		}

		glm::vec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), point);
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

	uint64_t MakeView(SceneRenderer& gpu, uint32_t width, uint32_t height, ESceneViewSky sky)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		renderer.SetSceneViewSky(view, sky, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	// Pixels black without bloom that it lights
	size_t CountBloomed(const RgbaImage& with, const RgbaImage& without)
	{
		size_t count = 0;
		for (size_t pixel = 0; pixel < with.Pixels.size(); pixel += 4)
		{
			const bool bBlack = without.Pixels[pixel] == 0 && without.Pixels[pixel + 1] == 0 && without.Pixels[pixel + 2] == 0;
			count += bBlack && int(with.Pixels[pixel]) + int(with.Pixels[pixel + 1]) + int(with.Pixels[pixel + 2]) > 6 ? 1 : 0;
		}
		return count;
	}

	void TestBloomSpreadsTheLight(SceneRenderer& gpu)
	{
		GlowScene scene(gpu.Renderer);
		gpu.Renderer.SetWorld(&scene.World);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight, ESceneViewSky::Black);

		const RgbaImage bloomed = Draw(gpu, view);
		SaveIfAsked(bloomed, "Glow");
		scene.Air().bBloom = false;
		const RgbaImage plain = Draw(gpu, view);
		SaveIfAsked(plain, "NoBloom");
		scene.Air().bBloom = true;

		// Round the cube and round the ring, black without bloom, lit with it
		const glm::vec2 besideCube = scene.Pixel(glm::vec3(-1.6f, 0.0f, -6.0f)) + glm::vec2(0.0f, -45.0f);
		const glm::vec2 besideRing = scene.Pixel(glm::vec3(3.0f, 0.0f, -10.0f)) + glm::vec2(0.0f, 42.0f);
		for (const auto& [pixel, what] : { std::pair{ besideCube, "above the cube" }, std::pair{ besideRing, "below the ring" } })
		{
			Require(Brightness(PixelAt(plain, pixel)) == 0, std::string("The wall ") + what + " isn't black without bloom: " + Text(PixelAt(plain, pixel)));
			Require(Brightness(PixelAt(bloomed, pixel)) > 15, std::string("The bloom doesn't light the wall ") + what + ": " + Text(PixelAt(bloomed, pixel)));
		}
		const size_t count = CountBloomed(bloomed, plain);
		Require(count > 5000, "The bloom lights only " + std::to_string(count) + " black pixels");
		// Far from both, the wall stays black
		Require(Brightness(PixelAt(bloomed, glm::vec2(4.0f, 4.0f))) <= 3, "The bloom lights the picture's far corner: " + Text(PixelAt(bloomed, glm::vec2(4.0f, 4.0f))));

		// A threshold above all of it, or no intensity: as without bloom, to the bit
		scene.Air().BloomThreshold = 100.0f;
		Require(Draw(gpu, view).Pixels == plain.Pixels, "A threshold above every light still blooms");
		scene.Air().BloomThreshold = 1.0f;
		scene.Air().BloomIntensity = 0.0f;
		Require(Draw(gpu, view).Pixels == plain.Pixels, "No intensity still blooms");
		scene.Air().BloomIntensity = BloomOnly().BloomIntensity;
		// Without the environment, as with its bloom off
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		Require(Draw(gpu, view).Pixels == plain.Pixels, "Without the environment the picture isn't as with bloom off");
		scene.World.Add<EnvironmentComponent>(scene.Environment, BloomOnly());

		// Exposed 6 stops down, the cube and the ring are under the threshold: nothing blooms
		gpu.Renderer.SetSceneViewExposure(view, -6.0f);
		const RgbaImage darkBloomed = Draw(gpu, view);
		scene.Air().bBloom = false;
		const RgbaImage darkPlain = Draw(gpu, view);
		scene.Air().bBloom = true;
		Require(darkBloomed.Pixels == darkPlain.Pixels, "Light exposed under the threshold blooms");
		gpu.Renderer.SetSceneViewExposure(view, 0.0f);

		// A wider radius spreads the light further: 60 pixels from the cube, brighter
		const glm::vec2 farFromCube = scene.Pixel(glm::vec3(-1.6f, 0.0f, -6.0f)) + glm::vec2(0.0f, -75.0f);
		scene.Air().BloomRadius = 0.3f;
		const RgbaImage narrow = Draw(gpu, view);
		scene.Air().BloomRadius = 0.9f;
		const RgbaImage wide = Draw(gpu, view);
		SaveIfAsked(wide, "Wide");
		scene.Air().BloomRadius = BloomOnly().BloomRadius;
		Require(Brightness(PixelAt(wide, farFromCube)) > Brightness(PixelAt(narrow, farFromCube)) + 6,
			"A wider radius doesn't spread further: " + Text(PixelAt(wide, farFromCube)) + " against " + Text(PixelAt(narrow, farFromCube)));

		// The chain: as many levels as the view's size allows of those asked for (5 of 6 here, the
		// last 15 by 8 pixels), the first half the view's size; fewer asked, fewer
		gpu.DrawFrames(1);
		const SceneViewBloom* bloom = gpu.Renderer.GetSceneViewBloom(view);
		Require(bloom && bloom->Levels == 5 && bloom->Levels == GetBloomLevelCount(ViewWidth, ViewHeight, 6) && bloom->Extent == vk::Extent2D(ViewWidth / 2, ViewHeight / 2),
			"The view's chain isn't 5 levels from half its size");
		scene.Air().BloomLevels = 2;
		gpu.DrawFrames(1);
		Require(gpu.Renderer.GetSceneViewBloom(view)->Levels == 2, "Two levels asked for aren't two");
		scene.Air().BloomLevels = 6;

		// Its GPU time, read after the frame: some while it blooms, none once it doesn't
		gpu.DrawFrames(2);
		const float bloomMilliseconds = gpu.Renderer.GetSceneViewGpuTimes(view)->Bloom;
		Require(bloomMilliseconds > 0.0f && bloomMilliseconds < 50.0f, "The bloom's GPU time isn't measured: " + std::to_string(bloomMilliseconds) + " ms");
		scene.Air().bBloom = false;
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->Bloom == 0.0f, "Bloom that is off takes GPU time");
		scene.Air().bBloom = true;

		// The debug view: the bloom alone, the cube and the ring dark in it but for their glow, the
		// far corner black
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Bloom);
		const RgbaImage alone = Draw(gpu, view);
		SaveIfAsked(alone, "DebugView");
		Require(Brightness(PixelAt(alone, besideCube)) > 15 && Brightness(PixelAt(alone, glm::vec2(4.0f, 4.0f))) <= 3, "The debug view doesn't show the bloom alone");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);
		scene.Air().bBloom = false;
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Bloom);
		const RgbaImage none = Draw(gpu, view);
		Require(Brightness(PixelAt(none, besideCube)) == 0, "The debug view of no bloom isn't black");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);
		scene.Air().bBloom = true;

		// A second, smaller view of the same world: a chain of its own, and its bloom
		const uint64_t small = MakeView(gpu, 200, 120, ESceneViewSky::Black);
		const RgbaImage smallBloomed = Draw(gpu, small);
		SaveIfAsked(smallBloomed, "SmallView");
		const SceneViewBloom* smallBloom = gpu.Renderer.GetSceneViewBloom(small);
		Require(smallBloom && smallBloom->Levels == GetBloomLevelCount(200, 120, 6) && smallBloom->Extent == vk::Extent2D(100, 60), "The small view's chain isn't its own");
		scene.Air().bBloom = false;
		const RgbaImage smallPlain = Draw(gpu, small);
		scene.Air().bBloom = true;
		Require(CountBloomed(smallBloomed, smallPlain) > 500, "The small view doesn't bloom");
		static_cast<IEditorRenderer&>(gpu.Renderer).DestroySceneView(small);

		RequireLooksLike(bloomed, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "Bloom.png");

		// The world taken away from a view that bloomed: its chain is let go of, and its tonemap
		// no longer reads it (a view without a world kept the last frame's set, whose bloom slot
		// pointed at the chain's freed image, and the device was lost)
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(4);
		Require(!gpu.Renderer.IsDeviceLost() && gpu.Renderer.GetSceneViewBloom(view)->Levels == 0, "A view whose world went away still blooms, or lost the device");
		static_cast<IEditorRenderer&>(gpu.Renderer).DestroySceneView(view);
		gpu.DrawFrames(1);
	}

	// The atmosphere test scene's glowing cube and decal bloom; its bloom off, they don't
	void TestAtmosphereTestSceneBlooms(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / "Scenes/AtmosphereTest.nyxscene", world, context),
			"The atmosphere test scene can't be loaded; NyxAtmosphereGpuTests --write-scene writes it");
		UpdateScatters(world);
		Entity air;
		size_t decals = 0;
		world.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent&)
			{
				air = entity;
			});
		world.Each<SurfaceDecalComponent>([&](Entity, const SurfaceDecalComponent&)
			{
				++decals;
			});
		Require(decals == 1, "The atmosphere test scene has no emissive decal");

		gpu.Renderer.SetWorld(&world);
		const uint64_t view = MakeView(gpu, ViewWidth, ViewHeight, ESceneViewSky::Scene);
		const RgbaImage bloomed = Draw(gpu, view, 3);
		SaveIfAsked(bloomed, "TestScene");
		world.Get<EnvironmentComponent>(air).bBloom = false;
		const RgbaImage plain = Draw(gpu, view, 2);
		size_t brighter = 0;
		for (size_t pixel = 0; pixel < bloomed.Pixels.size(); pixel += 4)
		{
			brighter += Brightness({ bloomed.Pixels[pixel], bloomed.Pixels[pixel + 1], bloomed.Pixels[pixel + 2] }) >
					Brightness({ plain.Pixels[pixel], plain.Pixels[pixel + 1], plain.Pixels[pixel + 2] }) + 6
				? 1
				: 0;
		}
		Require(brighter > 1000, "The atmosphere test scene's glowing cube and decal bloom only " + std::to_string(brighter) + " pixels");
		static_cast<IEditorRenderer&>(gpu.Renderer).DestroySceneView(view);
		gpu.Renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		SceneRenderer gpu("NyxBloomGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestBloomSpreadsTheLight(gpu);
		TestAtmosphereTestSceneBlooms(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxBloomGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxBloomGpuTests: all passed\n";
	return 0;
}

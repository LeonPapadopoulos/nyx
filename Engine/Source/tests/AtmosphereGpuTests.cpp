// The world's air on a real GPU (EnvironmentComponent). Under a black sky with an unlit sun, a black
// wall shows only the fog, so its pixels are what Atmosphere.h works out on the CPU, tonemapped:
// through level fog, from where the fog starts, held at its most opacity, and over a ground the
// fog thins above. The sky's horizon is the fog's colour, brighter towards the sun by the lobe.
// A decal over the fogged wall adds its light through the fog and nothing where it is empty; an
// instanced cube, a plain one and a two-sided quad's back are fogged alike; a box still shadows
// the fogged ground. The sky follows the sun: darker and warmer as it goes down, its glow round
// the sun, the light warmer through more air, the sky's light dimmer at night. Without the
// component, and with its fog, sky, bloom and ambient occlusion all off, the picture is the same to
// the bit. A scene of fog and a low sun looks as its reference picture does, and the atmosphere
// test scene loads and is fogged.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture. NyxAtmosphereGpuTests --write-scene writes
// Assets/Scenes/AtmosphereTest.nyxscene.
#include "Atmosphere.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GroundComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "ScatterComponent.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"
#include "AssetResolver.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Atmosphere-" + name + ".png");
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

	// The plane mesh stood up, facing +Z
	glm::quat FacingZ()
	{
		return glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	// Fog and nothing else: the fog's colour alone, no sun scattered, the sky as it is, no bloom, no
	// ambient occlusion
	EnvironmentComponent PlainFog(float density, float falloff, float height)
	{
		EnvironmentComponent environment;
		environment.bSkyFollowsSun = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.FogDensity = density;
		environment.FogHeightFalloff = falloff;
		environment.FogHeight = height;
		environment.FogColor = glm::vec3(0.5f, 0.6f, 0.8f);
		environment.FogSunScattering = 0.0f;
		return environment;
	}

	// A camera 2 m up looking along -Z, a black wall 40 m ahead facing it, under a black sky and a
	// sun that gives no light: what the view shows is the fog alone
	struct FogScene
	{
		Registry World;
		LiveMaterialFile BlackFile{ "Materials/AtmosphereTestBlack.nyxmat" };
		Material* Black = nullptr;
		Entity Wall;
		Entity Camera;
		Entity Sun;
		Entity Environment;
		CameraComponent Lens;

		explicit FogScene(VulkanRenderer& renderer)
		{
			BlackFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\n");
			Black = renderer.GetMaterial(BlackFile.Path);
			Wall = AddMesh(World, Placed(glm::vec3(0.0f, 2.0f, -40.0f), glm::vec3(200.0f, 1.0f, 200.0f), FacingZ()), renderer.GetPlaneMesh(), Black);
			World.Add<GuidComponent>(Wall, GuidComponent{ EntityGuid{ 0xF06 } });

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 2.0f, 0.0f)));
			World.Add<CameraComponent>(Camera, Lens);

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);

			Environment = World.CreateEntity();
			World.Add<EnvironmentComponent>(Environment, PlainFog(0.02f, 0.1f, 2.0f));
		}

		glm::vec3 TowardsLight() const
		{
			return -glm::normalize(World.Get<TransformComponent>(Sun).Rotation * glm::vec3(0.0f, 0.0f, -1.0f));
		}

		glm::vec3 LightColor() const
		{
			const DirectionalLightComponent& light = World.Get<DirectionalLightComponent>(Sun);
			return light.Color * light.Intensity;
		}

		SceneViewAtmosphere Atmosphere() const
		{
			return MakeSceneViewAtmosphere(World.Get<EnvironmentComponent>(Environment), TowardsLight(), LightColor());
		}

		glm::vec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, "A point the test looks at is out of the picture");
			return *pixel;
		}

		// The way the camera looks through the middle of a pixel
		glm::vec3 DirectionThrough(const glm::vec2& pixel) const
		{
			return ScreenPointToRay(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), glm::floor(pixel) + 0.5f).Direction;
		}

		// What a black surface at `point` shows through the fog, tonemapped
		std::array<uint8_t, 3> FoggedBlack(const glm::vec3& point) const
		{
			const SceneViewAtmosphere atmosphere = Atmosphere();
			const glm::vec3 camera = World.Get<TransformComponent>(Camera).Position;
			const float transmittance = GetHeightFogTransmittance(atmosphere, camera, point);
			const glm::vec3 inscattering = GetHeightFogInscattering(atmosphere, glm::normalize(point - camera), TowardsLight());
			return Tonemapped(inscattering * (1.0f - transmittance));
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
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

	void RequireNear(const std::array<uint8_t, 3>& actual, const std::array<uint8_t, 3>& expected, int tolerance, const std::string& what)
	{
		Require(Near(actual, expected, tolerance), what + ": " + Text(actual) + ", expected " + Text(expected));
	}

	// The fog on surfaces, against the CPU's: level, with a start distance, held at its most
	// opacity, and over a ground it thins above; none without the component
	void TestFogOnSurfaces(SceneRenderer& gpu)
	{
		FogScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const glm::vec3 wallMiddle(0.0f, 2.0f, -40.0f);
		const glm::vec2 middle = scene.Pixel(wallMiddle);

		// Level with the fog's height, as thick all the way: exp(-0.02 * 40)
		const RgbaImage level = Draw(gpu, view);
		SaveIfAsked(level, "Level");
		RequireNear(PixelAt(level, middle), scene.FoggedBlack(wallMiddle), 2, "The wall 40 m away through level fog");
		Require(Brightness(PixelAt(level, middle)) > 120, "Level fog over 40 m hides too little: " + Text(PixelAt(level, middle)));

		// From 30 m on: a quarter as much
		EnvironmentComponent& environment = scene.World.Get<EnvironmentComponent>(scene.Environment);
		environment.FogStartDistance = 30.0f;
		RequireNear(PixelAt(Draw(gpu, view), middle), scene.FoggedBlack(wallMiddle), 2, "The wall through fog from 30 m on");
		environment.FogStartDistance = 0.0f;

		// At most 30 % opaque, however thick
		environment.FogDensity = 0.5f;
		environment.FogMaxOpacity = 0.3f;
		const std::array<uint8_t, 3> held = PixelAt(Draw(gpu, view), middle);
		RequireNear(held, Tonemapped(environment.FogColor * 0.3f), 2, "Thick fog held at 30 % opacity");
		environment.FogDensity = 0.02f;
		environment.FogMaxOpacity = 1.0f;

		// A ground the fog thins above, from 6 m up: thicker further away and lower down, as the
		// CPU works it out along each ray
		scene.World.Get<MeshRendererComponent>(scene.Wall).bVisible = false;
		const Entity ground = AddMesh(scene.World, Placed(glm::vec3(0.0f), glm::vec3(400.0f, 1.0f, 400.0f)), gpu.Renderer.GetPlaneMesh(), scene.Black);
		TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
		camera.Position = glm::vec3(0.0f, 6.0f, 0.0f);
		camera.Rotation = glm::quat(glm::radians(glm::vec3(-20.0f, 0.0f, 0.0f)));
		environment = PlainFog(0.03f, 0.25f, 0.0f);
		const RgbaImage overGround = Draw(gpu, view);
		SaveIfAsked(overGround, "OverGround");
		for (const float distance : { 8.0f, 16.0f, 40.0f })
		{
			const glm::vec3 point(0.0f, 0.0f, -distance);
			RequireNear(PixelAt(overGround, scene.Pixel(point)), scene.FoggedBlack(point), 2, "The ground " + std::to_string(int(distance)) + " m away");
		}
		Require(Brightness(PixelAt(overGround, scene.Pixel(glm::vec3(0.0f, 0.0f, -40.0f)))) > Brightness(PixelAt(overGround, scene.Pixel(glm::vec3(0.0f, 0.0f, -8.0f)))),
			"The ground isn't more fogged further away");
		scene.World.DestroyEntity(ground);
		camera.Position = glm::vec3(0.0f, 2.0f, 0.0f);
		camera.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		scene.World.Get<MeshRendererComponent>(scene.Wall).bVisible = true;

		// Without the component, and with its fog off: the black wall
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		RequireNear(PixelAt(Draw(gpu, view), middle), { 0, 0, 0 }, 0, "The wall without an environment");
		EnvironmentComponent fogOff = PlainFog(0.02f, 0.1f, 2.0f);
		fogOff.bFog = false;
		scene.World.Add<EnvironmentComponent>(scene.Environment, fogOff);
		RequireNear(PixelAt(Draw(gpu, view), middle), { 0, 0, 0 }, 0, "The wall with the fog off");

		EndView(gpu, view);
	}

	// The sky's horizon through the fog: its colour, and the sun's light scattered towards the
	// camera round the sun, by the lobe
	void TestFogOnTheSky(SceneRenderer& gpu)
	{
		FogScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Wall).bVisible = false;
		// A low sun ahead, a little to the left, that gives light
		scene.World.Get<TransformComponent>(scene.Sun).Rotation = glm::angleAxis(glm::radians(190.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(glm::radians(-6.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		scene.World.Get<DirectionalLightComponent>(scene.Sun).Intensity = 2.0f;
		EnvironmentComponent& environment = scene.World.Get<EnvironmentComponent>(scene.Environment);
		environment = PlainFog(0.01f, 0.2f, 0.0f);
		environment.FogSunScattering = 1.0f;
		environment.FogSunLobe = 0.7f;
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const RgbaImage sky = Draw(gpu, view);
		SaveIfAsked(sky, "SkyHorizon");

		const SceneViewAtmosphere atmosphere = scene.Atmosphere();
		const glm::vec3 camera = scene.World.Get<TransformComponent>(scene.Camera).Position;
		const auto expected = [&](const glm::vec2& pixel)
		{
			const glm::vec3 direction = scene.DirectionThrough(pixel);
			const float transmittance = GetHeightFogTransmittance(atmosphere, camera, camera + direction * SkyFogDistance);
			return Tonemapped(GetHeightFogInscattering(atmosphere, direction, scene.TowardsLight()) * (1.0f - transmittance));
		};
		// On the horizon towards the sun, the other side of the picture, and higher up, where the
		// ray leaves the fog sooner
		const glm::vec2 towardsSun = scene.Pixel(camera + glm::vec3(scene.TowardsLight().x, 0.02f, scene.TowardsLight().z) * 100.0f);
		const glm::vec2 aside(ViewWidth - 20.0f, towardsSun.y);
		const glm::vec2 higher(towardsSun.x, towardsSun.y - 60.0f);
		for (const auto& [pixel, what] : { std::pair{ towardsSun, "towards the sun" }, std::pair{ aside, "aside" }, std::pair{ higher, "higher up" } })
		{
			RequireNear(PixelAt(sky, pixel), expected(pixel), 2, std::string("The sky's horizon ") + what);
		}
		Require(Brightness(PixelAt(sky, towardsSun)) > Brightness(PixelAt(sky, aside)) + 20, "The fog doesn't glow towards the sun");
		Require(Brightness(PixelAt(sky, higher)) + 20 < Brightness(PixelAt(sky, towardsSun)), "The sky isn't clearer higher up");
		EndView(gpu, view);
	}

	// What draws over and among what is fogged: a decal adds its light through the fog and nothing
	// where it is empty; an instanced cube, a plain one and a two-sided quad's back are fogged
	// alike; and a box still shadows the fogged ground
	void TestFogOnDecalsInstancesAndShadows(SceneRenderer& gpu)
	{
		FogScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const glm::vec3 wallMiddle(0.0f, 2.0f, -40.0f);

		// A ring 8 m across on the wall: its middle shows the fogged wall, its ring glows dimmer
		// through the fog than without it
		const Entity decalEntity = scene.World.CreateEntity();
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0xF06 }, .LocalPosition = glm::vec3(0.0f), .LocalNormal = glm::vec3(0.0f, 1.0f, 0.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = glm::vec2(8.0f, 8.0f);
		decal.MaterialAsset = gpu.Renderer.GetMaterial("Materials/MaskedDecal.nyxmat");
		decal.Color = glm::vec3(1.0f, 0.5f, 0.2f);
		decal.Intensity = 2.0f;
		scene.World.Add<SurfaceDecalComponent>(decalEntity, decal);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage withDecal = Draw(gpu, view);
		SaveIfAsked(withDecal, "Decal");
		scene.World.Get<SurfaceDecalComponent>(decalEntity).bVisible = false;
		const RgbaImage withoutDecal = Draw(gpu, view);
		const glm::vec2 ringMiddle = scene.Pixel(wallMiddle + glm::vec3(0.0f, 0.0f, SurfaceDecalLift));
		RequireNear(PixelAt(withDecal, ringMiddle), PixelAt(withoutDecal, ringMiddle), 1, "The decal's empty middle over the fogged wall");
		// On the ring, about 3.3 m out from the middle (DecalRing.png)
		const glm::vec2 onRing = scene.Pixel(wallMiddle + glm::vec3(3.3f, 0.0f, 0.0f));
		const int foggedGlow = Brightness(PixelAt(withDecal, onRing)) - Brightness(PixelAt(withoutDecal, onRing));
		scene.World.Get<EnvironmentComponent>(scene.Environment).bFog = false;
		scene.World.Get<SurfaceDecalComponent>(decalEntity).bVisible = true;
		const int clearGlow = Brightness(PixelAt(Draw(gpu, view), onRing));
		Require(foggedGlow > 0 && foggedGlow + 30 < clearGlow, "The ring doesn't glow dimmer through the fog: " + std::to_string(foggedGlow) + " over the fog against " + std::to_string(clearGlow) + " without");
		scene.World.Get<EnvironmentComponent>(scene.Environment).bFog = true;
		scene.World.DestroyEntity(decalEntity);

		// A plain cube on the left, one instanced on the right, both 20 m away, and a two-sided
		// quad between them, its back to the camera: fogged as the same distance is
		const Entity plain = AddMesh(scene.World, Placed(glm::vec3(-4.0f, 2.0f, -20.5f)), gpu.Renderer.GetCubeMesh(), scene.Black);
		const Entity instanced = AddMesh(scene.World, Placed(glm::vec3(0.0f)), gpu.Renderer.GetCubeMesh(), scene.Black);
		auto instances = std::make_shared<MeshInstances>();
		instances->Transforms.push_back(Placed(glm::vec3(4.0f, 2.0f, -20.5f)).ToMatrix());
		MeshInstancesComponent meshInstances;
		SetMeshInstances(meshInstances, std::move(instances));
		scene.World.Add<MeshInstancesComponent>(instanced, meshInstances);
		LiveMaterialFile twoSidedFile("Materials/AtmosphereTestBlackTwoSided.nyxmat");
		twoSidedFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\nTwoSided = true\n");
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 2.0f, -20.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))), gpu.Renderer.GetPlaneMesh(),
			gpu.Renderer.GetMaterial(twoSidedFile.Path));
		gpu.DrawUntilShadersCompiled();
		const RgbaImage alike = Draw(gpu, view);
		SaveIfAsked(alike, "Alike");
		const std::array<uint8_t, 3> plainPixel = PixelAt(alike, scene.Pixel(glm::vec3(-4.0f, 2.0f, -20.0f)));
		RequireNear(plainPixel, scene.FoggedBlack(glm::vec3(-4.0f, 2.0f, -20.0f)), 2, "The plain cube 20 m away");
		RequireNear(PixelAt(alike, scene.Pixel(glm::vec3(4.0f, 2.0f, -20.0f))), plainPixel, 1, "The instanced cube against the plain one");
		RequireNear(PixelAt(alike, scene.Pixel(glm::vec3(0.0f, 2.0f, -20.0f))), plainPixel, 1, "The two-sided quad's back against the cube");
		scene.World.DestroyEntity(plain);
		scene.World.DestroyEntity(instanced);
		EndView(gpu, view);

		// A box's shadow on a white ground through thin fog: still darker than the sun beside it
		Registry world;
		Material* white = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		AddMesh(world, Placed(glm::vec3(0.0f), glm::vec3(60.0f, 1.0f, 60.0f)), gpu.Renderer.GetPlaneMesh(), white);
		AddMesh(world, Placed(glm::vec3(0.0f, 1.5f, -10.0f)), gpu.Renderer.GetCubeMesh(), white);
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-25.0f, 0.0f, 0.0f)))));
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-90.0f, 0.0f, 0.0f)))));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity air = world.CreateEntity();
		world.Add<EnvironmentComponent>(air, PlainFog(0.02f, 0.1f, 0.0f));
		const uint64_t shadowView = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage shadowed = Draw(gpu, shadowView, 3);
		SaveIfAsked(shadowed, "Shadow");
		const std::optional<glm::vec2> underBox = WorldToScreenPoint(world.Get<TransformComponent>(camera), CameraComponent{}, glm::vec2(ViewWidth, ViewHeight), glm::vec3(0.0f, 0.0f, -10.0f));
		const std::optional<glm::vec2> beside = WorldToScreenPoint(world.Get<TransformComponent>(camera), CameraComponent{}, glm::vec2(ViewWidth, ViewHeight), glm::vec3(3.0f, 0.0f, -10.0f));
		Require(underBox && beside, "The shadow is out of the picture");
		Require(Brightness(PixelAt(shadowed, *underBox)) + 30 < Brightness(PixelAt(shadowed, *beside)),
			"The box doesn't shadow the fogged ground: " + Text(PixelAt(shadowed, *underBox)) + " against " + Text(PixelAt(shadowed, *beside)));
		EndView(gpu, shadowView);
	}

	// The scene's own sky, looked at straight up and on the horizon away from the sun, and a white
	// ground, for a sun at `degrees` above the horizon
	struct SkyScene
	{
		Registry World;
		Entity Ground;
		Entity Camera;
		Entity Sun;
		Entity Environment;

		explicit SkyScene(VulkanRenderer& renderer)
		{
			Ground = AddMesh(World, Placed(glm::vec3(0.0f), glm::vec3(100.0f, 1.0f, 100.0f)), renderer.GetPlaneMesh(), renderer.GetMaterial("Materials/Untextured.nyxmat"));
			Camera = World.CreateEntity();
			// Looking north and up, the ground in the bottom of the picture
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(20.0f, 0.0f, 0.0f)))));
			World.Add<CameraComponent>(Camera, CameraComponent{});
			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f)));
			DirectionalLightComponent light;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);
			Environment = World.CreateEntity();
			EnvironmentComponent environment;
			environment.bFog = false;
			World.Add<EnvironmentComponent>(Environment, environment);
		}

		// The sun `degrees` above the horizon, behind the camera
		void SetSun(float degrees)
		{
			World.Get<TransformComponent>(Sun).Rotation = glm::angleAxis(glm::radians(-degrees), glm::vec3(1.0f, 0.0f, 0.0f));
		}
	};

	float RedOverBlue(const std::array<uint8_t, 3>& rgb)
	{
		return float(rgb[0]) / std::max(float(rgb[2]), 1.0f);
	}

	// The sky follows the sun: darker and warmer as it sets, its light too, and the light itself
	// warmer through more air; the glow round the sun; nothing changed with it all off
	void TestSkyFollowsTheSun(SceneRenderer& gpu)
	{
		SkyScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Scene);
		const glm::vec2 skyPixel(ViewWidth * 0.5f, 20.0f);
		const glm::vec2 groundPixel(ViewWidth * 0.5f, ViewHeight - 10.0f);

		scene.SetSun(60.0f);
		const RgbaImage high = Draw(gpu, view);
		SaveIfAsked(high, "SunHigh");
		scene.SetSun(4.0f);
		const RgbaImage low = Draw(gpu, view);
		SaveIfAsked(low, "SunLow");
		scene.SetSun(-20.0f);
		const RgbaImage night = Draw(gpu, view);
		SaveIfAsked(night, "Night");

		const std::array<uint8_t, 3> highSky = PixelAt(high, skyPixel);
		const std::array<uint8_t, 3> lowSky = PixelAt(low, skyPixel);
		Require(Brightness(lowSky) < Brightness(highSky), "The sky isn't darker with the sun low: " + Text(lowSky) + " against " + Text(highSky));
		Require(RedOverBlue(lowSky) > RedOverBlue(highSky) * 1.15f, "The sky isn't warmer with the sun low: " + Text(lowSky) + " against " + Text(highSky));
		Require(Brightness(PixelAt(night, skyPixel)) * 3 < Brightness(highSky), "The night sky isn't dark: " + Text(PixelAt(night, skyPixel)));
		// The ground, lit by the low sun through more air, warmer than by the high one; at night,
		// with only the night sky's light, dark
		Require(RedOverBlue(PixelAt(low, groundPixel)) > RedOverBlue(PixelAt(high, groundPixel)) * 1.15f,
			"The low sun doesn't light the ground warmer: " + Text(PixelAt(low, groundPixel)) + " against " + Text(PixelAt(high, groundPixel)));
		Require(Brightness(PixelAt(night, groundPixel)) * 4 < Brightness(PixelAt(high, groundPixel)), "The ground at night isn't dark: " + Text(PixelAt(night, groundPixel)));

		// Without reddening, the low sun's light is as white as the light
		scene.World.Get<EnvironmentComponent>(scene.Environment).SunReddening = 0.0f;
		scene.SetSun(4.0f);
		const RgbaImage notReddened = Draw(gpu, view);
		Require(RedOverBlue(PixelAt(notReddened, groundPixel)) < RedOverBlue(PixelAt(low, groundPixel)),
			"Without reddening the low sun's light is still as warm: " + Text(PixelAt(notReddened, groundPixel)));
		scene.World.Get<EnvironmentComponent>(scene.Environment).SunReddening = 1.0f;

		// The glow round the sun: the camera turned to a sun 10 degrees up ahead
		scene.SetSun(10.0f);
		TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
		camera.Rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::quat(glm::radians(glm::vec3(10.0f, 0.0f, 0.0f)));
		const glm::vec2 middle(ViewWidth * 0.5f, ViewHeight * 0.5f);
		const RgbaImage glowing = Draw(gpu, view);
		SaveIfAsked(glowing, "SunGlow");
		scene.World.Get<EnvironmentComponent>(scene.Environment).SunGlow = 0.0f;
		const RgbaImage noGlow = Draw(gpu, view);
		Require(Brightness(PixelAt(glowing, middle)) > Brightness(PixelAt(noGlow, middle)) + 30, "No glow round the sun: " + Text(PixelAt(glowing, middle)) + " against " + Text(PixelAt(noGlow, middle)));

		// Fog, sky, bloom and ambient occlusion all off: as without the component, to the bit
		scene.World.Get<EnvironmentComponent>(scene.Environment).bSkyFollowsSun = false;
		scene.World.Get<EnvironmentComponent>(scene.Environment).bBloom = false;
		scene.World.Get<EnvironmentComponent>(scene.Environment).bAmbientOcclusion = false;
		const RgbaImage allOff = Draw(gpu, view);
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		const RgbaImage none = Draw(gpu, view);
		Require(allOff.Pixels == none.Pixels, "Fog, sky, bloom and ambient occlusion off don't draw the picture as without the component");
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/AtmosphereTest.nyxscene";

	Entity AddEntity(Registry& world, const std::string& name, uint64_t guid, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ guid } });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	// The ground test scene with small cubes scattered over its ground, a hazy environment, the sun
	// low in the south-west, and for bloom (NyxBloomGpuTests) a glowing cube and a glowing ring on
	// the nearest crate, as the tests expect it
	void WriteScene()
	{
		const std::filesystem::path assets = Paths::GetAssetsDir();
		Registry world;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(assets / "Scenes/GroundTest.nyxscene", world, context), "The ground test scene can't be loaded");
		EntityGuid ground;
		world.Each<GroundComponent>([&](Entity entity, const GroundComponent&)
			{
				ground = world.Get<GuidComponent>(entity).Guid;
			});
		world.Each<DirectionalLightComponent>([&](Entity entity, const DirectionalLightComponent&)
			{
				world.Get<TransformComponent>(entity).Rotation = glm::quat(glm::radians(glm::vec3(-14.0f, -35.0f, 0.0f)));
			});

		const Entity cubes = AddEntity(world, "Cubes", 0xA7305C0BE5ull, Placed(glm::vec3(0.0f)));
		MeshRendererComponent meshRenderer;
		meshRenderer.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" };
		meshRenderer.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" };
		world.Add<MeshRendererComponent>(cubes, meshRenderer);
		ScatterComponent scatter;
		scatter.Surface = ground;
		scatter.Size = glm::vec2(96.0f, 96.0f);
		scatter.Density = 4.0f;
		scatter.Spacing = 0.3f;
		scatter.ScaleRange = glm::vec2(0.12f, 0.3f);
		scatter.SinkDepth = 0.03f;
		world.Add<ScatterComponent>(cubes, scatter);

		// A cube of an emissive material floating over the ground, and an emissive decal, a ring, on
		// the front of the first crate
		const Entity glowingCube = AddEntity(world, "Glowing Cube", 0xA7305C0BE6ull,
			Placed(glm::vec3(1.6f, 1.3f, -1.5f), glm::vec3(0.45f), glm::quat(glm::radians(glm::vec3(25.0f, 40.0f, 10.0f)))));
		MeshRendererComponent glowing;
		glowing.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" };
		glowing.Material = AssetReference{ .Type = "Material", .Path = "Materials/Emissive.nyxmat" };
		world.Add<MeshRendererComponent>(glowingCube, glowing);
		EntityGuid crate;
		world.Each<NameComponent>([&](Entity entity, const NameComponent& name)
			{
				if (name.Name == "Crate 1")
				{
					crate = world.Get<GuidComponent>(entity).Guid;
				}
			});
		const Entity ring = AddEntity(world, "Glowing Ring", 0xA7305DECAull, Placed(glm::vec3(0.0f)));
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = crate, .LocalPosition = glm::vec3(0.0f, 0.0f, 0.5f), .LocalNormal = glm::vec3(0.0f, 0.0f, 1.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = glm::vec2(0.5f, 0.5f);
		decal.Material = AssetReference{ .Type = "Material", .Path = "Materials/MaskedDecal.nyxmat" };
		decal.Color = glm::vec3(1.0f, 0.55f, 0.15f);
		decal.Intensity = 6.0f;
		world.Add<SurfaceDecalComponent>(ring, decal);

		// Hazier than the defaults, across a valley 128 m wide
		const Entity air = AddEntity(world, "Environment", 0xA7305E2Full, Placed(glm::vec3(0.0f)));
		EnvironmentComponent environment;
		environment.FogDensity = 0.015f;
		environment.FogHeightFalloff = 0.12f;
		world.Add<EnvironmentComponent>(air, environment);

		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The atmosphere test scene as the game loads it: its scattered cubes, its far hillsides
	// fogged against the near ground
	void TestAtmosphereTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The atmosphere test scene can't be loaded; NyxAtmosphereGpuTests --write-scene writes it");
		UpdateScatters(world);
		size_t cubes = 0;
		world.Each<MeshInstancesComponent>([&](Entity, const MeshInstancesComponent& instances)
			{
				cubes += instances.Instances ? instances.Instances->Transforms.size() : 0;
			});
		Require(cubes > 20000, "The atmosphere test scene scatters only " + std::to_string(cubes) + " cubes");

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage fogged = Draw(gpu, view, 3);
		SaveIfAsked(fogged, "TestScene");
		Entity air;
		world.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent&)
			{
				air = entity;
			});
		world.Get<EnvironmentComponent>(air).bFog = false;
		const RgbaImage clear = Draw(gpu, view, 2);
		SaveIfAsked(clear, "TestSceneNoFog");
		size_t changed = 0;
		for (size_t pixel = 0; pixel < fogged.Pixels.size(); pixel += 4)
		{
			changed += Brightness({ fogged.Pixels[pixel], fogged.Pixels[pixel + 1], fogged.Pixels[pixel + 2] }) !=
					Brightness({ clear.Pixels[pixel], clear.Pixels[pixel + 1], clear.Pixels[pixel + 2] })
				? 1
				: 0;
		}
		Require(changed > fogged.Pixels.size() / 4 / 4, "The atmosphere test scene's fog changes only " + std::to_string(changed) + " pixels");
		EndView(gpu, view);
	}

	// A crate yard in fog under a low sun, against References/Atmosphere.png
	void TestAtmosphereScene(SceneRenderer& gpu)
	{
		Registry world;
		Material* plastic = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		AddMesh(world, Placed(glm::vec3(0.0f), glm::vec3(300.0f, 1.0f, 300.0f)), gpu.Renderer.GetPlaneMesh(), plastic);
		for (int row = 0; row < 6; ++row)
		{
			for (int column = -2; column <= 2; ++column)
			{
				AddMesh(world, Placed(glm::vec3(column * 6.0f + (row % 2) * 3.0f, 1.0f, -8.0f - row * 14.0f), glm::vec3(2.0f)), gpu.Renderer.GetCubeMesh(), plastic);
			}
		}
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-4.0f, 0.0f, 0.0f)))));
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-8.0f, 25.0f, 0.0f)))));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		// The fog and the sky alone: bloom and ambient occlusion have pictures of their own
		// (NyxBloomGpuTests, NyxAmbientOcclusionGpuTests)
		const Entity air = world.CreateEntity();
		EnvironmentComponent environment;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		world.Add<EnvironmentComponent>(air, environment);

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "Scene");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "Atmosphere.png");
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
		SceneRenderer gpu("NyxAtmosphereGpuTests");
		TestFogOnSurfaces(gpu);
		TestFogOnTheSky(gpu);
		TestFogOnDecalsInstancesAndShadows(gpu);
		TestSkyFollowsTheSun(gpu);
		TestAtmosphereScene(gpu);
		TestAtmosphereTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxAtmosphereGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxAtmosphereGpuTests: all passed\n";
	return 0;
}

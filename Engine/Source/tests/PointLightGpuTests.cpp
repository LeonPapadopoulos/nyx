// Point lights on a real GPU (PointLightComponent). Over a grey matte floor under a black sky and a
// sun that gives no light, seen from above, a floor pixel shows a light's light alone, so it is what
// the CPU works out (GetPointLightFalloff() and the same physically based light), tonemapped: below
// the light and further out, fading to black at the range and black past it; a coloured light; two
// lights adding up; a light under height fog, dimmed by the fog's transmittance; a light behind the
// camera whose light reaches into view. Ambient occlusion never dims a light's light. A light
// wholly out of view is culled, and the picture is the same with it and without it. Without
// lights, the picture is the same to the bit as before any was added. In the editor's scene views a
// click on a light's icon selects it, and a selected light's range is drawn. A scene of coloured
// lights at dusk looks as its reference picture does.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture. NyxPointLightGpuTests --write-scene writes
// Assets/Scenes/LightsTest.nyxscene, the atmosphere test scene at dusk with point lights.
#include "Atmosphere.h"
#include "AssetResolver.h"
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
#include "PointLightComponent.h"
#include "PointLights.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
	constexpr float Pi = 3.14159265358979f;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("PointLights-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, const glm::ivec2& pixel)
	{
		const size_t offset = (static_cast<size_t>(pixel.y) * image.Width + pixel.x) * 4;
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

	Entity AddLight(Registry& world, const glm::vec3& position, const PointLightComponent& light)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, Placed(position));
		world.Add<PointLightComponent>(entity, light);
		return entity;
	}

	// nyx::directLight() on the CPU: what a light from `l` of colour `radiance` sends towards `v`
	glm::vec3 DirectLight(const glm::vec3& baseColor, float metallic, float roughness, const glm::vec3& n, const glm::vec3& v, const glm::vec3& l, const glm::vec3& radiance)
	{
		const float perceptual = std::clamp(roughness, 0.045f, 1.0f);
		const float alpha = perceptual * perceptual;
		const glm::vec3 f0 = glm::mix(glm::vec3(0.04f), baseColor, metallic);
		const glm::vec3 diffuseColor = baseColor * (1.0f - metallic);
		const float nDotV = std::max(glm::dot(n, v), 1e-4f);
		const glm::vec3 h = glm::normalize(v + l);
		const float nDotL = std::clamp(glm::dot(n, l), 0.0f, 1.0f);
		const float nDotH = std::clamp(glm::dot(n, h), 0.0f, 1.0f);
		const float lDotH = std::clamp(glm::dot(l, h), 0.0f, 1.0f);
		const float a2 = alpha * alpha;
		const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
		const float distribution = a2 / (Pi * d * d);
		const float lambdaV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
		const float lambdaL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
		const float visibility = 0.5f / std::max(lambdaV + lambdaL, 1e-6f);
		const glm::vec3 fresnel = f0 + (1.0f - f0) * std::pow(1.0f - lDotH, 5.0f);
		return (diffuseColor + Pi * distribution * visibility * fresnel) * radiance * nDotL;
	}

	// A grey matte floor seen from 6 m straight above, under a black sky and a sun that gives no
	// light: what the view shows is the point lights' light alone
	struct FloorScene
	{
		static constexpr glm::vec3 Grey{ 0.5f, 0.5f, 0.5f };
		static constexpr float Roughness = 0.8f;

		Registry World;
		LiveMaterialFile GreyFile{ "Materials/PointLightTestGrey.nyxmat" };
		Material* GreyMaterial = nullptr;
		Entity Floor;
		Entity Camera;
		CameraComponent Lens;

		explicit FloorScene(VulkanRenderer& renderer)
		{
			GreyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.5 0.5 0.5\nMetallic = 0\nRoughness = 0.8\n");
			GreyMaterial = renderer.GetMaterial(GreyFile.Path);
			Floor = AddMesh(World, Placed(glm::vec3(0.0f), glm::vec3(100.0f, 1.0f, 100.0f)), renderer.GetPlaneMesh(), GreyMaterial);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 6.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-90.0f, 0.0f, 0.0f)))));
			World.Add<CameraComponent>(Camera, Lens);

			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);
		}

		glm::vec3 CameraPosition() const
		{
			return World.Get<TransformComponent>(Camera).Position;
		}

		// The pixel a point of the floor shows in
		glm::ivec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, "A point the test looks at is out of the picture");
			return glm::ivec2(glm::floor(*pixel));
		}

		// Where the floor is at the middle of a pixel
		glm::vec3 FloorAt(const glm::ivec2& pixel) const
		{
			const Ray ray = ScreenPointToRay(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), glm::vec2(pixel) + 0.5f);
			return ray.Origin + ray.Direction * (-ray.Origin.y / ray.Direction.y);
		}

		// The light the floor sends towards the camera at `point`, from every light of the world
		glm::vec3 LightAt(const glm::vec3& point) const
		{
			const glm::vec3 n(0.0f, 1.0f, 0.0f);
			const glm::vec3 v = glm::normalize(CameraPosition() - point);
			glm::vec3 sum(0.0f);
			World.Each<PointLightComponent>([&](Entity entity, const PointLightComponent& light)
				{
					const glm::vec3 toLight = World.Get<TransformComponent>(entity).Position - point;
					const float distance = glm::length(toLight);
					const glm::vec3 radiance = light.Color * light.Intensity * GetPointLightFalloff(distance, light.Range);
					sum += DirectLight(Grey, 0.0f, Roughness, n, v, toLight / distance, radiance);
				});
			return sum;
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

	// The floor's pixel nearest `point` against the CPU's light there, tonemapped
	void RequireLitAsExpected(const FloorScene& scene, const RgbaImage& image, const glm::vec3& point, const std::string& what, int tolerance = 2)
	{
		const glm::ivec2 pixel = scene.Pixel(point);
		RequireNear(PixelAt(image, pixel), Tonemapped(scene.LightAt(scene.FloorAt(pixel))), tolerance, what);
	}

	// One white light 1 m above the floor, reaching 4 m: below it and out to its range as the CPU
	// works it out, black at and past the range, and nothing anywhere without it
	void TestOneLight(SceneRenderer& gpu)
	{
		FloorScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const RgbaImage dark = Draw(gpu, view);
		for (size_t pixel = 0; pixel < dark.Pixels.size(); pixel += 4)
		{
			Require(dark.Pixels[pixel] == 0 && dark.Pixels[pixel + 1] == 0 && dark.Pixels[pixel + 2] == 0, "Without a light, the floor isn't black");
		}

		const Entity light = AddLight(scene.World, glm::vec3(0.0f, 1.0f, 0.0f), PointLightComponent{ .Intensity = 1.0f, .Range = 4.0f });
		const RgbaImage lit = Draw(gpu, view);
		SaveIfAsked(lit, "OneLight");
		RequireLitAsExpected(scene, lit, glm::vec3(0.0f), "Below the light");
		Require(Brightness(PixelAt(lit, scene.Pixel(glm::vec3(0.0f)))) > 300, "Below the light, too dark: " + Text(PixelAt(lit, scene.Pixel(glm::vec3(0.0f)))));
		for (const float x : { 0.5f, 1.0f, 2.0f, 3.0f, 3.5f })
		{
			RequireLitAsExpected(scene, lit, glm::vec3(x, 0.0f, 0.0f), "The floor " + std::to_string(x) + " m out");
		}
		Require(Brightness(PixelAt(lit, scene.Pixel(glm::vec3(3.0f, 0.0f, 0.0f)))) > 0, "3 m out, inside the range, nothing");

		// Past the range (√15 m out at the floor), nothing: the same along every direction
		for (const glm::vec3 point : { glm::vec3(3.95f, 0.0f, 0.0f), glm::vec3(-4.2f, 0.0f, 0.0f), glm::vec3(-3.5f, 0.0f, -1.8f), glm::vec3(3.0f, 0.0f, 2.7f) })
		{
			RequireNear(PixelAt(lit, scene.Pixel(point)), { 0, 0, 0 }, 0, "Past the range");
		}

		// Brighter, and further: four times as bright, the range the same, still black past it
		scene.World.Get<PointLightComponent>(light).Intensity = 4.0f;
		const RgbaImage brighter = Draw(gpu, view);
		RequireLitAsExpected(scene, brighter, glm::vec3(2.0f, 0.0f, 0.0f), "Four times as bright, 2 m out");
		RequireNear(PixelAt(brighter, scene.Pixel(glm::vec3(3.95f, 0.0f, 0.0f))), { 0, 0, 0 }, 0, "Four times as bright, past the range");
		scene.World.Get<PointLightComponent>(light).Range = 6.0f;
		const RgbaImage further = Draw(gpu, view);
		RequireLitAsExpected(scene, further, glm::vec3(4.5f, 0.0f, 0.0f), "Reaching 6 m, 4.5 m out");
		Require(Brightness(PixelAt(further, scene.Pixel(glm::vec3(3.95f, 0.0f, 0.0f)))) > 0, "Reaching 6 m, nothing 4 m out");

		// Moved with its entity
		scene.World.Get<TransformComponent>(light).Position = glm::vec3(-1.0f, 0.5f, 0.0f);
		RequireLitAsExpected(scene, Draw(gpu, view), glm::vec3(-1.0f, 0.0f, 0.0f), "Below the moved light");

		// Without it, black again to the bit
		scene.World.DestroyEntity(light);
		Require(Draw(gpu, view).Pixels == dark.Pixels, "Without its light, the floor isn't black again");
		EndView(gpu, view);
	}

	// A coloured light, and two lights adding up where both reach
	void TestColouredAndTwoLights(SceneRenderer& gpu)
	{
		FloorScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);

		const Entity orange = AddLight(scene.World, glm::vec3(0.0f, 1.0f, 0.0f), PointLightComponent{ .Color = glm::vec3(1.0f, 0.4f, 0.1f), .Intensity = 1.5f, .Range = 4.0f });
		const RgbaImage coloured = Draw(gpu, view);
		SaveIfAsked(coloured, "Coloured");
		for (const float x : { 0.0f, 1.0f, 2.5f })
		{
			RequireLitAsExpected(scene, coloured, glm::vec3(x, 0.0f, 0.0f), "The orange light " + std::to_string(x) + " m out");
		}
		const std::array<uint8_t, 3> below = PixelAt(coloured, scene.Pixel(glm::vec3(0.0f)));
		Require(below[0] > below[1] && below[1] > below[2], "The orange light isn't orange: " + Text(below));
		scene.World.DestroyEntity(orange);

		// Blue on the left, red on the right, 2 m apart: between them both, each where the other
		// doesn't reach alone
		AddLight(scene.World, glm::vec3(-1.0f, 1.0f, 0.0f), PointLightComponent{ .Color = glm::vec3(0.2f, 0.4f, 1.0f), .Intensity = 1.0f, .Range = 3.0f });
		const RgbaImage blue = Draw(gpu, view);
		AddLight(scene.World, glm::vec3(1.0f, 1.0f, 0.0f), PointLightComponent{ .Color = glm::vec3(1.0f, 0.2f, 0.1f), .Intensity = 1.0f, .Range = 3.0f });
		const RgbaImage both = Draw(gpu, view);
		SaveIfAsked(both, "TwoLights");
		for (const float x : { -2.5f, -1.0f, 0.0f, 0.5f, 1.0f, 2.5f })
		{
			RequireLitAsExpected(scene, both, glm::vec3(x, 0.0f, 0.0f), "Both lights " + std::to_string(x) + " m out");
		}
		const glm::ivec2 middle = scene.Pixel(glm::vec3(0.0f));
		Require(PixelAt(both, middle)[0] > PixelAt(blue, middle)[0] + 20, "Between them, the red light doesn't add to the blue one");
		// Past the red light's range on the left, only the blue one's
		const glm::ivec2 left = scene.Pixel(glm::vec3(-2.9f, 0.0f, 0.0f));
		RequireNear(PixelAt(both, left), PixelAt(blue, left), 0, "Past the red light's range, the blue one's alone");
		EndView(gpu, view);
	}

	// A light under height fog: its light dimmed by the fog's transmittance to the camera, the fog's
	// own light added as everywhere
	void TestLightUnderFog(SceneRenderer& gpu)
	{
		FloorScene scene(gpu.Renderer);
		const Entity air = scene.World.CreateEntity();
		EnvironmentComponent environment;
		environment.bSkyFollowsSun = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.FogDensity = 0.08f;
		environment.FogHeightFalloff = 0.0f;
		environment.FogColor = glm::vec3(0.05f, 0.06f, 0.08f);
		environment.FogSunScattering = 0.0f;
		scene.World.Add<EnvironmentComponent>(air, environment);
		AddLight(scene.World, glm::vec3(0.0f, 1.0f, 0.0f), PointLightComponent{ .Color = glm::vec3(1.0f, 0.8f, 0.6f), .Intensity = 2.0f, .Range = 5.0f });
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const RgbaImage fogged = Draw(gpu, view);
		SaveIfAsked(fogged, "UnderFog");

		const SceneViewAtmosphere atmosphere = MakeSceneViewAtmosphere(environment, glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)), glm::vec3(0.0f));
		for (const float x : { 0.0f, 1.5f, 3.0f })
		{
			const glm::ivec2 pixel = scene.Pixel(glm::vec3(x, 0.0f, 0.0f));
			const glm::vec3 point = scene.FloorAt(pixel);
			const float transmittance = GetHeightFogTransmittance(atmosphere, scene.CameraPosition(), point);
			const glm::vec3 inscattering = GetHeightFogInscattering(atmosphere, glm::normalize(point - scene.CameraPosition()), glm::vec3(0.0f, 1.0f, 0.0f));
			Require(transmittance < 0.7f, "The fog is too thin to test");
			RequireNear(PixelAt(fogged, pixel), Tonemapped(scene.LightAt(point) * transmittance + inscattering * (1.0f - transmittance)), 2,
				"Under fog, " + std::to_string(x) + " m out");
		}
		EndView(gpu, view);
	}

	// A light behind the camera, its light reaching the floor in view; one wholly out of view culled,
	// changing nothing; ambient occlusion never dimming a light's light
	void TestCullingAndOcclusion(SceneRenderer& gpu)
	{
		FloorScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);

		// 2 m above the camera, which looks down: behind it, but reaching 10 m
		AddLight(scene.World, glm::vec3(0.0f, 8.0f, 0.0f), PointLightComponent{ .Intensity = 30.0f, .Range = 10.0f });
		const RgbaImage behind = Draw(gpu, view);
		SaveIfAsked(behind, "Behind");
		RequireLitAsExpected(scene, behind, glm::vec3(0.0f), "Lit by a light behind the camera");
		RequireLitAsExpected(scene, behind, glm::vec3(3.0f, 0.0f, 1.0f), "Lit by a light behind the camera, off the middle");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->PointLights == 1 && stats->PointLightsCulled == 0, "The light behind the camera was culled");

		// 50 m away, reaching 10 m: culled, the picture the same to the bit
		AddLight(scene.World, glm::vec3(50.0f, 1.0f, 0.0f), PointLightComponent{ .Intensity = 30.0f, .Range = 10.0f });
		const RgbaImage withFar = Draw(gpu, view);
		stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats->PointLights == 2 && stats->PointLightsCulled == 1 && stats->PointLightsDropped == 0, "The light out of view wasn't culled");
		Require(withFar.Pixels == behind.Pixels, "A light out of view changes the picture");

		// Ambient occlusion, round a box on the floor: under the black sky there is no sky's light
		// for it to dim, and it never dims the lights': the same to the bit, on or off
		AddMesh(scene.World, Placed(glm::vec3(1.0f, 0.5f, 0.0f)), gpu.Renderer.GetCubeMesh(), scene.GreyMaterial);
		const Entity air = scene.World.CreateEntity();
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bSkyFollowsSun = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.OcclusionIntensity = 4.0f;
		scene.World.Add<EnvironmentComponent>(air, environment);
		const RgbaImage occlusionOff = Draw(gpu, view);
		scene.World.Get<EnvironmentComponent>(air).bAmbientOcclusion = true;
		const RgbaImage occlusionOn = Draw(gpu, view);
		SaveIfAsked(occlusionOn, "Occlusion");
		Require(occlusionOn.Pixels == occlusionOff.Pixels, "Ambient occlusion dims the point lights' light");
		EndView(gpu, view);
	}

	// In the editor's scene views: a click on a light's icon selects the light, one beside it what is
	// beneath; a selected light's range drawn round it in the selection's colour (with debug drawing)
	void TestEditorIcon(SceneRenderer& gpu)
	{
		FloorScene scene(gpu.Renderer);
		IEditorRenderer& renderer = gpu.Renderer;
		const glm::vec3 position(1.5f, 1.0f, 0.0f);
		const Entity light = AddLight(scene.World, position, PointLightComponent{ .Intensity = 1.0f, .Range = 2.0f });
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		renderer.SetSceneViewShowEditorOverlays(view, true);
		gpu.DrawFrames(2);

		const glm::ivec2 icon = scene.Pixel(position);
		renderer.RequestPick(view, icon.x, icon.y);
		gpu.DrawFrames(3);
		IEditorRenderer::PickResult pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && pick.HitEntity == light, "A click on the light's icon doesn't pick the light");

		const glm::ivec2 beside = scene.Pixel(position + glm::vec3(0.6f, -1.0f, 0.0f));
		renderer.RequestPick(view, beside.x, beside.y);
		gpu.DrawFrames(3);
		pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && pick.HitEntity == scene.Floor, "A click beside the light's icon doesn't pick the floor");

#if NYX_DEBUG_DRAW
		// The range's circle round the light, level with it, in the selection's colour
		const glm::ivec2 onRange = scene.Pixel(position + glm::vec3(2.0f, 0.0f, 0.0f));
		const auto mostOrange = [&](const RgbaImage& image)
		{
			int most = -1000;
			for (int y = onRange.y - 2; y <= onRange.y + 2; ++y)
			{
				for (int x = onRange.x - 2; x <= onRange.x + 2; ++x)
				{
					const std::array<uint8_t, 3> rgb = PixelAt(image, glm::ivec2(x, y));
					most = std::max(most, int(rgb[0]) + int(rgb[1]) - 2 * int(rgb[2]));
				}
			}
			return most;
		};
		const int unselected = mostOrange(Draw(gpu, view));
		renderer.SetSelectedEntity(light);
		const RgbaImage selected = Draw(gpu, view);
		SaveIfAsked(selected, "Selected");
		Require(mostOrange(selected) > unselected + 150, "The selected light's range isn't drawn round it");
		renderer.SetSelectedEntity(std::nullopt);
#endif
		EndView(gpu, view);
	}

	// Lights in a crate yard at dusk under fog, coloured, against References/PointLights.png
	void TestLightsScene(SceneRenderer& gpu)
	{
		Registry world;
		Material* plastic = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		LiveMaterialFile greyFile("Materials/PointLightTestYard.nyxmat");
		greyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.6 0.6 0.6\nMetallic = 0\nRoughness = 0.7\n");
		Material* grey = gpu.Renderer.GetMaterial(greyFile.Path);
		AddMesh(world, Placed(glm::vec3(0.0f), glm::vec3(300.0f, 1.0f, 300.0f)), gpu.Renderer.GetPlaneMesh(), grey);
		for (int row = 0; row < 4; ++row)
		{
			for (int column = -2; column <= 2; ++column)
			{
				AddMesh(world, Placed(glm::vec3(column * 4.0f + (row % 2) * 2.0f, 0.75f, -4.0f - row * 5.0f), glm::vec3(1.5f)), gpu.Renderer.GetCubeMesh(), plastic);
			}
		}
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-12.0f, 0.0f, 0.0f)))));
		world.Add<CameraComponent>(camera, CameraComponent{});
		// The sun just below the horizon
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(2.0f, 25.0f, 0.0f)))));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity air = world.CreateEntity();
		EnvironmentComponent environment;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		environment.FogDensity = 0.02f;
		world.Add<EnvironmentComponent>(air, environment);

		const glm::vec3 colours[] = { { 1.0f, 0.55f, 0.2f }, { 0.3f, 0.5f, 1.0f }, { 0.4f, 1.0f, 0.4f }, { 1.0f, 0.3f, 0.8f }, { 1.0f, 0.9f, 0.7f }, { 0.3f, 1.0f, 1.0f } };
		for (int index = 0; index < 12; ++index)
		{
			const float x = (index % 4) * 4.0f - 6.0f;
			const float z = -1.5f - (index / 4) * 6.0f;
			AddLight(world, glm::vec3(x, 0.4f + (index % 3) * 0.6f, z), PointLightComponent{ .Color = colours[index % 6], .Intensity = 3.0f, .Range = 5.0f });
		}

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "Scene");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "PointLights.png");

		// Without its lights, darker everywhere they reach
		world.Each<PointLightComponent>([&](Entity, PointLightComponent& light)
			{
				light.Intensity = 0.0f;
			});
		const RgbaImage unlit = Draw(gpu, view);
		size_t darker = 0;
		for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
		{
			darker += image.Pixels[pixel] + image.Pixels[pixel + 1] + image.Pixels[pixel + 2] > unlit.Pixels[pixel] + unlit.Pixels[pixel + 1] + unlit.Pixels[pixel + 2] + 6 ? 1 : 0;
		}
		Require(darker > image.Pixels.size() / 4 / 5, "The lights light only " + std::to_string(darker) + " pixels");
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/LightsTest.nyxscene";

	Entity AddEntity(Registry& world, const std::string& name, uint64_t guid, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ guid } });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	// The atmosphere test scene at dusk, the sun just set, with coloured point lights among its
	// crates and cubes; the glowing cube and the glowing ring each with a small light of their
	// colour, as a game pairs an emissive surface with a light
	void WriteScene()
	{
		const std::filesystem::path assets = Paths::GetAssetsDir();
		Registry world;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(assets / "Scenes/AtmosphereTest.nyxscene", world, context), "The atmosphere test scene can't be loaded");
		world.Each<DirectionalLightComponent>([&](Entity entity, const DirectionalLightComponent&)
			{
				world.Get<TransformComponent>(entity).Rotation = glm::quat(glm::radians(glm::vec3(4.0f, -35.0f, 0.0f)));
			});
		glm::vec3 glowingCube(0.0f);
		world.Each<NameComponent>([&](Entity entity, const NameComponent& name)
			{
				if (name.Name == "Glowing Cube")
				{
					glowingCube = world.Get<TransformComponent>(entity).Position;
				}
			});

		struct Lamp
		{
			const char* Name;
			glm::vec3 Position;
			PointLightComponent Light;
		};
		const Lamp lamps[] = {
			{ "Glowing Cube Light", glowingCube, { .Color = { 1.0f, 0.85f, 0.6f }, .Intensity = 2.0f, .Range = 6.0f } },
			{ "Glowing Ring Light", { -2.0f, 0.7f, -3.3f }, { .Color = { 1.0f, 0.55f, 0.15f }, .Intensity = 0.8f, .Range = 3.0f } },
			{ "Blue Light", { -4.5f, 1.2f, 1.5f }, { .Color = { 0.3f, 0.5f, 1.0f }, .Intensity = 3.0f, .Range = 7.0f } },
			{ "Green Light", { 4.5f, 0.6f, 2.5f }, { .Color = { 0.4f, 1.0f, 0.45f }, .Intensity = 2.0f, .Range = 5.0f } },
			{ "Pink Light", { 0.5f, 1.0f, -9.0f }, { .Color = { 1.0f, 0.35f, 0.75f }, .Intensity = 5.0f, .Range = 9.0f } },
			{ "Warm Light", { -7.0f, 2.0f, -6.0f }, { .Color = { 1.0f, 0.6f, 0.3f }, .Intensity = 6.0f, .Range = 10.0f } },
		};
		uint64_t guid = 0xA7305116A7ull;
		for (const Lamp& lamp : lamps)
		{
			const Entity entity = AddEntity(world, lamp.Name, guid++, Placed(lamp.Position));
			world.Add<PointLightComponent>(entity, lamp.Light);
		}

		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The lights test scene as the game loads it: its lights extracted, and lighting it
	void TestLightsTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The lights test scene can't be loaded; NyxPointLightGpuTests --write-scene writes it");
		size_t lights = 0;
		world.Each<PointLightComponent>([&](Entity, const PointLightComponent&)
			{
				++lights;
			});
		Require(lights == 6, "The lights test scene has " + std::to_string(lights) + " lights, not 6");

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage lit = Draw(gpu, view, 3);
		SaveIfAsked(lit, "TestScene");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->PointLights == 6 && stats->PointLightsCulled < 6, "The lights test scene's lights aren't in view");
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
		SceneRenderer gpu("NyxPointLightGpuTests");
		TestOneLight(gpu);
		TestColouredAndTwoLights(gpu);
		TestLightUnderFog(gpu);
		TestCullingAndOcclusion(gpu);
		TestEditorIcon(gpu);
		TestLightsScene(gpu);
		TestLightsTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxPointLightGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxPointLightGpuTests: all passed\n";
	return 0;
}

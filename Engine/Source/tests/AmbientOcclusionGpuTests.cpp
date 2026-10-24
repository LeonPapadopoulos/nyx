// Ambient occlusion on a real GPU (VulkanAmbientOcclusion). A box on a floor under the studio's sky
// and a sun that gives no light: the floor at the box's foot is darker with occlusion, the open
// floor and the box's top as they were, within a step of 255. Lit by the sun alone, under a black
// sky, the picture is the same with and without it, to the bit: it never dims the sun's light. A
// wider radius darkens further from the box; no intensity, or no environment, changes nothing; each
// quality darkens the corner, and the open floor shows no noise. A dense field of thin instanced
// blades, like grass, is occluded down among them, its noise and cost measured. Over a decal and
// through fog it dims only the sky's light that reaches the camera. The debug view shows the
// occlusion. The scene looks as its reference picture does.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "AmbientOcclusion.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("AmbientOcclusion-" + name + ".png");
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

	// Ambient occlusion alone: no fog, the sky as it is, no bloom
	EnvironmentComponent OcclusionOnly()
	{
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bSkyFollowsSun = false;
		environment.bBloom = false;
		return environment;
	}

	// A light grey floor, a box of 2 m on it, a camera looking down at the box's foot from the
	// front, a sun that gives no light (the studio's sky alone lights it)
	struct CornerScene
	{
		Registry World;
		LiveMaterialFile GreyFile{ "Materials/OcclusionTestGrey.nyxmat" };
		Material* Grey = nullptr;
		Entity Floor;
		Entity Box;
		Entity Camera;
		Entity Sun;
		Entity Environment;
		CameraComponent Lens;

		explicit CornerScene(VulkanRenderer& renderer)
		{
			GreyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.7 0.7 0.7\nMetallic = 0\nRoughness = 0.9\n");
			Grey = renderer.GetMaterial(GreyFile.Path);
			Floor = AddMesh(World, Placed(glm::vec3(0.0f), glm::vec3(60.0f, 1.0f, 60.0f)), renderer.GetPlaneMesh(), Grey);
			World.Add<GuidComponent>(Floor, GuidComponent{ EntityGuid{ 0xF100 } });
			Box = AddMesh(World, Placed(glm::vec3(0.0f, 1.0f, -6.0f), glm::vec3(2.0f)), renderer.GetCubeMesh(), Grey);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-20.0f, 0.0f, 0.0f)))));
			World.Add<CameraComponent>(Camera, Lens);

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);

			Environment = World.CreateEntity();
			World.Add<EnvironmentComponent>(Environment, OcclusionOnly());
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

	// How much a square of pixels round `center`, `radius` across, differs from pixel to pixel: the
	// mean of the brightness steps between neighbours, across and down
	double Roughness(const RgbaImage& image, const glm::vec2& center, int radius)
	{
		double sum = 0.0;
		int count = 0;
		for (int y = -radius; y < radius; ++y)
		{
			for (int x = -radius; x < radius; ++x)
			{
				const glm::vec2 at = center + glm::vec2(float(x), float(y));
				const int here = Brightness(PixelAt(image, at));
				sum += std::abs(here - Brightness(PixelAt(image, at + glm::vec2(1.0f, 0.0f)))) + std::abs(here - Brightness(PixelAt(image, at + glm::vec2(0.0f, 1.0f))));
				count += 2;
			}
		}
		return sum / count;
	}

	void TestTheBoxsFootIsOccluded(SceneRenderer& gpu)
	{
		CornerScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Studio);
		const RgbaImage occluded = Draw(gpu, view);
		SaveIfAsked(occluded, "Corner");
		scene.Air().bAmbientOcclusion = false;
		const RgbaImage open = Draw(gpu, view);
		SaveIfAsked(open, "CornerOff");
		scene.Air().bAmbientOcclusion = true;

		// At the box's foot, in front: darker. The open floor far from it, and the box's top: as
		// they were
		const glm::vec2 foot = scene.Pixel(glm::vec3(0.0f, 0.0f, -4.9f));
		const glm::vec2 openFloor = scene.Pixel(glm::vec3(-3.5f, 0.0f, -6.0f));
		const glm::vec2 top = scene.Pixel(glm::vec3(0.3f, 2.0f, -6.3f));
		Require(Brightness(PixelAt(occluded, foot)) * 10 < Brightness(PixelAt(open, foot)) * 9,
			"The box's foot isn't darker with occlusion: " + Text(PixelAt(occluded, foot)) + " against " + Text(PixelAt(open, foot)));
		Require(Near(PixelAt(occluded, openFloor), PixelAt(open, openFloor), 3), "The open floor is occluded: " + Text(PixelAt(occluded, openFloor)) + " against " + Text(PixelAt(open, openFloor)));
		// The box's top, seen nearly edge on, darkens a little: a plane at a grazing angle finds a
		// little of itself above its horizon (2.5 % at most here)
		Require(Near(PixelAt(occluded, top), PixelAt(open, top), 6), "The box's top is occluded: " + Text(PixelAt(occluded, top)) + " against " + Text(PixelAt(open, top)));
		// No noise on the open floor
		const double openRoughness = Roughness(occluded, openFloor, 12);
		std::cout << "Open floor, mean step between neighbours: " << openRoughness << " (without occlusion " << Roughness(open, openFloor, 12) << ")\n";
		Require(openRoughness < Roughness(open, openFloor, 12) + 0.5, "The open floor is noisy with occlusion");

		// A wider radius darkens further from the box: 0.8 m in front of it
		const glm::vec2 near = scene.Pixel(glm::vec3(0.0f, 0.0f, -4.2f));
		scene.Air().OcclusionRadius = 0.3f;
		const RgbaImage tight = Draw(gpu, view);
		scene.Air().OcclusionRadius = 3.0f;
		const RgbaImage wide = Draw(gpu, view);
		SaveIfAsked(wide, "Wide");
		scene.Air().OcclusionRadius = OcclusionOnly().OcclusionRadius;
		Require(Brightness(PixelAt(wide, near)) + 9 < Brightness(PixelAt(tight, near)),
			"A wider radius doesn't darken further: " + Text(PixelAt(wide, near)) + " against " + Text(PixelAt(tight, near)));

		// More intensity, darker; none, as off, to the bit; and as without the environment
		scene.Air().OcclusionIntensity = 4.0f;
		Require(Brightness(PixelAt(Draw(gpu, view), foot)) < Brightness(PixelAt(occluded, foot)), "More intensity isn't darker");
		scene.Air().OcclusionIntensity = 0.0f;
		Require(Draw(gpu, view).Pixels == open.Pixels, "No intensity still occludes");
		scene.Air().OcclusionIntensity = OcclusionOnly().OcclusionIntensity;
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		Require(Draw(gpu, view).Pixels == open.Pixels, "Without the environment the picture isn't as with occlusion off");
		scene.World.Add<EnvironmentComponent>(scene.Environment, OcclusionOnly());

		// Every quality darkens the foot, and keeps the open floor
		for (const EAmbientOcclusionQuality quality : { EAmbientOcclusionQuality::Low, EAmbientOcclusionQuality::Medium, EAmbientOcclusionQuality::High })
		{
			scene.Air().OcclusionQuality = quality;
			const RgbaImage image = Draw(gpu, view);
			SaveIfAsked(image, "Quality" + std::to_string(int(quality)));
			Require(Brightness(PixelAt(image, foot)) * 10 < Brightness(PixelAt(open, foot)) * 9 && Near(PixelAt(image, openFloor), PixelAt(open, openFloor), 3),
				"Quality " + std::to_string(int(quality)) + " doesn't occlude the foot alone");
		}
		scene.Air().OcclusionQuality = EAmbientOcclusionQuality::Medium;

		// The debug view: the occlusion, dark at the foot, white on the open floor
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::AmbientOcclusion);
		const RgbaImage buffer = Draw(gpu, view);
		SaveIfAsked(buffer, "DebugView");
		Require(Brightness(PixelAt(buffer, foot)) < 600 && Brightness(PixelAt(buffer, openFloor)) > 740, "The debug view doesn't show the occlusion: " + Text(PixelAt(buffer, foot)) + ", open " +
			Text(PixelAt(buffer, openFloor)));
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);

		// Its GPU time, read after the frame
		gpu.DrawFrames(2);
		const float milliseconds = gpu.Renderer.GetSceneViewGpuTimes(view)->AmbientOcclusion;
		Require(milliseconds > 0.0f && milliseconds < 50.0f, "The occlusion's GPU time isn't measured: " + std::to_string(milliseconds) + " ms");

		RequireLooksLike(occluded, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "AmbientOcclusion.png");
		EndView(gpu, view);
	}

	// Lit by the sun alone, under a black sky: the same with occlusion and without, to the bit
	void TestTheSunsLightIsNeverOccluded(SceneRenderer& gpu)
	{
		CornerScene scene(gpu.Renderer);
		scene.World.Get<DirectionalLightComponent>(scene.Sun).Intensity = 2.0f;
		scene.World.Get<DirectionalLightComponent>(scene.Sun).bCastShadows = true;
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const RgbaImage occluded = Draw(gpu, view, 3);
		SaveIfAsked(occluded, "SunOnly");
		scene.Air().bAmbientOcclusion = false;
		const RgbaImage open = Draw(gpu, view, 3);
		Require(occluded.Pixels == open.Pixels, "Occlusion dims the sun's light");
		const glm::vec2 foot = scene.Pixel(glm::vec3(0.0f, 0.0f, -4.9f));
		Require(Brightness(PixelAt(occluded, foot)) > 100, "The sun doesn't light the box's foot");
		EndView(gpu, view);
	}

	// Through fog, the occlusion dims what reaches the camera of the sky's light, not the fog's own:
	// in thick fog the foot is nearly the open floor; over a decal that covers, the decal's own
	// sky's light is dimmed as the floor's was
	void TestFogAndDecals(SceneRenderer& gpu)
	{
		CornerScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Studio);
		const glm::vec2 foot = scene.Pixel(glm::vec3(0.0f, 0.0f, -4.9f));

		scene.Air().bFog = true;
		scene.Air().FogDensity = 0.6f;
		scene.Air().FogHeightFalloff = 0.0f;
		const RgbaImage foggedOccluded = Draw(gpu, view);
		SaveIfAsked(foggedOccluded, "Fog");
		scene.Air().bAmbientOcclusion = false;
		const RgbaImage foggedOpen = Draw(gpu, view);
		Require(std::abs(Brightness(PixelAt(foggedOccluded, foot)) - Brightness(PixelAt(foggedOpen, foot))) <= 6,
			"Thick fog in front of the foot is occluded: " + Text(PixelAt(foggedOccluded, foot)) + " against " + Text(PixelAt(foggedOpen, foot)));
		scene.Air().bAmbientOcclusion = true;
		scene.Air().bFog = false;

		// A grey decal that covers the floor at the foot: occluded as the floor was
		LiveMaterialFile coverFile("Materials/OcclusionTestCover.nyxmat");
		coverFile.Write("Shader = Shaders/MaskedDecal.slang\nMask = Textures/DecalRing.png\nCover = 1\nThreshold = 0.0\n");
		const Entity decalEntity = scene.World.CreateEntity();
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0xF100 }, .LocalPosition = glm::vec3(0.0f, 0.0f, -4.9f / 60.0f), .LocalNormal = glm::vec3(0.0f, 1.0f, 0.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = glm::vec2(1.0f, 0.4f);
		decal.MaterialAsset = gpu.Renderer.GetMaterial(coverFile.Path);
		decal.Intensity = 0.0f;
		scene.World.Add<SurfaceDecalComponent>(decalEntity, decal);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage decalOccluded = Draw(gpu, view);
		SaveIfAsked(decalOccluded, "Decal");
		scene.Air().bAmbientOcclusion = false;
		const RgbaImage decalOpen = Draw(gpu, view);
		scene.Air().bAmbientOcclusion = true;
		std::cout << "The decal at the foot: " << Text(PixelAt(decalOccluded, foot)) << " occluded, " << Text(PixelAt(decalOpen, foot)) << " open\n";
		Require(Brightness(PixelAt(decalOccluded, foot)) <= Brightness(PixelAt(decalOpen, foot)), "A decal at the foot is brighter with occlusion");
		EndView(gpu, view);
	}

	// A dense field of thin blades drawn instanced, as grass: occluded down among them, the
	// occlusion's noise and its GPU time printed
	void TestDenseInstancedField(SceneRenderer& gpu)
	{
		CornerScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Box).bVisible = false;
		auto instances = std::make_shared<MeshInstances>();
		uint32_t state = 12345u;
		const auto random = [&]
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(state >> 8) / 16777216.0f;
		};
		for (int index = 0; index < 20000; ++index)
		{
			const glm::vec3 at((random() - 0.5f) * 14.0f, 0.0f, -2.0f - random() * 14.0f);
			const float height = 0.25f + random() * 0.3f;
			instances->Transforms.push_back(Placed(at + glm::vec3(0.0f, height * 0.5f, 0.0f), glm::vec3(0.025f, height, 0.025f),
				glm::angleAxis(random() * 6.2831853f, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis((random() - 0.5f) * 0.6f, glm::vec3(1.0f, 0.0f, 0.0f))).ToMatrix());
		}
		const Entity field = AddMesh(scene.World, Placed(glm::vec3(0.0f)), gpu.Renderer.GetCubeMesh(), scene.Grey);
		MeshInstancesComponent meshInstances;
		SetMeshInstances(meshInstances, std::move(instances));
		scene.World.Add<MeshInstancesComponent>(field, meshInstances);
		scene.World.Get<TransformComponent>(scene.Camera).Position = glm::vec3(0.0f, 1.2f, 0.0f);

		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Studio);
		const glm::vec2 amongBlades(ViewWidth * 0.5f, ViewHeight * 0.45f);
		for (const EAmbientOcclusionQuality quality : { EAmbientOcclusionQuality::Low, EAmbientOcclusionQuality::Medium, EAmbientOcclusionQuality::High })
		{
			scene.Air().OcclusionQuality = quality;
			const RgbaImage image = Draw(gpu, view, 3);
			gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::AmbientOcclusion);
			const RgbaImage buffer = Draw(gpu, view, 2);
			gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);
			gpu.DrawFrames(2);
			SaveIfAsked(image, "Field" + std::to_string(int(quality)));
			SaveIfAsked(buffer, "FieldDebug" + std::to_string(int(quality)));
			double mean = 0.0;
			for (int y = -20; y < 20; ++y)
			{
				for (int x = -40; x < 40; ++x)
				{
					mean += Brightness(PixelAt(buffer, amongBlades + glm::vec2(float(x), float(y)))) / 3.0;
				}
			}
			mean /= 80.0 * 40.0;
			std::cout << "Dense field, quality " << int(quality) << ": occlusion among the blades " << mean / 255.0 << ", mean step between neighbours " << Roughness(buffer, amongBlades, 20) / 3.0
					  << " of 255 (the lit picture's " << Roughness(image, amongBlades, 20) / 3.0 << "), " << gpu.Renderer.GetSceneViewGpuTimes(view)->AmbientOcclusion
					  << " ms on the GPU\n";
			Require(mean < 235.0, "The field isn't occluded among its blades: " + std::to_string(mean));
		}
		EndView(gpu, view);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		SceneRenderer gpu("NyxAmbientOcclusionGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestTheBoxsFootIsOccluded(gpu);
		TestTheSunsLightIsNeverOccluded(gpu);
		TestFogAndDecals(gpu);
		TestDenseInstancedField(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxAmbientOcclusionGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxAmbientOcclusionGpuTests: all passed\n";
	return 0;
}

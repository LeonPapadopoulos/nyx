// An SDF terrain's chunks on a real GPU: hills with tunnels and a floating landmass, made round the
// camera at three levels of detail, drawn by the terrain's Mesh Renderer, one object a chunk; a
// click on them picks the terrain; the debug view of levels and seams draws them in their colours;
// moved, the terrain is drawn where it went. Its occlusion volume darkens a closed chamber's sky
// light to next to nothing and leaves open ground as it was, and shades ground past the sun's
// shadow distance under a slab held over it.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "EnvironmentComponent.h"
#include "Entity.h"
#include "ImageFile.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "SdfTerrainComponent.h"
#include "SdfTerrain.h"
#include "SceneSerializer.h"
#include "TerrainChunks.h"
#include "TerrainGate3Region.h"
#include "TerrainWater.h"
#include "TerrainOcclusion.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 640;
	constexpr uint32_t ViewHeight = 360;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("TerrainChunks-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 7;
		terrain.ViewDistance = 160.0f;
		terrain.LevelDistance = 2.0f;
		terrain.Layers.clear();
		SdfTerrainLayer hills;
		hills.Amplitude = 14.0f;
		hills.Frequency = 0.01f;
		hills.TerraceHeight = 5.0f;
		hills.TerraceSharpness = 0.7f;
		terrain.Layers.push_back(hills);
		SdfTerrainLayer tunnels;
		tunnels.Kind = ESdfTerrainLayerKind::Tunnels;
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = 6.0f;
		tunnels.Depth = 30.0f;
		tunnels.Radius = 7.0f;
		tunnels.Frequency = 0.02f;
		tunnels.Octaves = 2;
		terrain.Layers.push_back(tunnels);
		SdfTerrainLayer island;
		island.Kind = ESdfTerrainLayerKind::Landmass;
		island.Height = 60.0f;
		island.Depth = 15.0f;
		island.Amplitude = 3.0f;
		island.Center = glm::vec2(20.0f, -70.0f);
		island.Radius = 40.0f;
		island.UndersideDepth = 25.0f;
		island.EdgeRoughness = 12.0f;
		island.Frequency = 0.02f;
		island.Material = 2;
		terrain.Layers.push_back(island);
		terrain.SharpMaterials = { 2 };
		return terrain;
	}

	struct TerrainScene
	{
		SceneRenderer& Gpu;
		Registry World;
		uint64_t View = 0;
		Entity Terrain;
		Entity Camera;
		glm::vec3 CameraPosition{ -10.0f, 45.0f, 60.0f };

		explicit TerrainScene(SceneRenderer& gpu, const SdfTerrainComponent& terrain = MakeTerrain())
			: Gpu(gpu)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera,
				TransformComponent{ .Position = CameraPosition, .Rotation = glm::quat(glm::radians(glm::vec3(-22.0f, -8.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
			World.Add<CameraComponent>(Camera, CameraComponent{});
			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-45.0f, 35.0f, 0.0f))) });
			World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

			Terrain = World.CreateEntity();
			World.Add<TransformComponent>(Terrain, TransformComponent{});
			World.Add<SdfTerrainComponent>(Terrain, terrain);
			MeshRendererComponent meshRenderer;
			meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
			World.Add<MeshRendererComponent>(Terrain, meshRenderer);

			Nyx::IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			View = renderer.CreateSceneView();
			// The editor's overlays for picking; nothing here has an icon but the sun, which is above the view
			renderer.SetSceneViewShowEditorOverlays(View, true);
			renderer.SetSceneViewCameraMode(View, Nyx::EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
		}

		~TerrainScene()
		{
			Nyx::IEditorRenderer& renderer = Gpu.Renderer;
			renderer.DestroySceneView(View);
			renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		void Look(const glm::vec3& position, const glm::vec3& at)
		{
			CameraPosition = position;
			TransformComponent& transform = World.Get<TransformComponent>(Camera);
			transform.Position = position;
			transform.Rotation = glm::quatLookAt(glm::normalize(at - position), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		// Frames until the chunks round the camera are all in, and its occlusion volume if it keeps one
		void MakeChunks()
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(World, { &CameraPosition, 1 }, JobSystem::Get());
				const TerrainChunksStats& stats = World.Get<TerrainChunksComponent>(Terrain).Stats;
				const SdfTerrainComponent& values = World.Get<SdfTerrainComponent>(Terrain);
				const bool bOcclusion = (values.SkyOcclusion <= 0.0f && !values.bFarShadows) ||
										(World.Has<TerrainOcclusionComponent>(Terrain) && World.Get<TerrainOcclusionComponent>(Terrain).bReady &&
											!World.Get<TerrainOcclusionComponent>(Terrain).bFilling);
				if (stats.bSettled && stats.Pending == 0 && stats.Resident > 0 && bOcclusion)
				{
					return;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The chunks should be made within a minute");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}

		RgbaImage Draw()
		{
			Gpu.DrawFrames(3);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}
	};

	// The picture's mean brightness, 0 to 255
	double MeanBrightness(const RgbaImage& image)
	{
		double sum = 0.0;
		for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
		{
			sum += (double(image.Pixels[pixel]) + image.Pixels[pixel + 1] + image.Pixels[pixel + 2]) / 3.0;
		}
		return sum / double(image.Pixels.size() / 4);
	}

	size_t CountChanged(const RgbaImage& a, const RgbaImage& b)
	{
		size_t changed = 0;
		for (size_t pixel = 0; pixel < a.Pixels.size(); pixel += 4)
		{
			changed += (a.Pixels[pixel] != b.Pixels[pixel] || a.Pixels[pixel + 1] != b.Pixels[pixel + 1] || a.Pixels[pixel + 2] != b.Pixels[pixel + 2]) ? 1 : 0;
		}
		return changed;
	}

	// The chunks drawn, one object each, the terrain picked by a click on them, and drawn elsewhere
	// once moved
	void TestChunksDrawn(SceneRenderer& gpu)
	{
		TerrainScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.World.Get<MeshRendererComponent>(scene.Terrain).bVisible = false;
		const RgbaImage sky = scene.Draw();
		scene.World.Get<MeshRendererComponent>(scene.Terrain).bVisible = true;

		scene.MakeChunks();
		const RgbaImage drawn = scene.Draw();
		SaveIfAsked(drawn, "Terrain");
		const TerrainChunksComponent& chunks = scene.World.Get<TerrainChunksComponent>(scene.Terrain);
		const size_t covered = CountChanged(sky, drawn);
		Require(covered > ViewWidth * ViewHeight / 3, "The terrain should cover much of the view, not " + std::to_string(covered) + " pixels");
		std::cout << "Terrain: " << chunks.Stats.Resident << " chunks, " << chunks.Stats.WithSeams << " with seams, " << chunks.Stats.Triangles << " triangles, "
				  << covered << " pixels\n";
		Require(chunks.Stats.WithSeams > 0, "The terrain should be drawn at more than one level");

		Nyx::IEditorRenderer& renderer = gpu.Renderer;
		renderer.RequestPick(scene.View, ViewWidth / 2, ViewHeight * 3 / 4);
		gpu.DrawFrames(3);
		const Nyx::IEditorRenderer::PickResult picked = renderer.ConsumeLastPickResult(scene.View);
		Require(picked.bHasNewResult && picked.HitEntity == scene.Terrain, "A click on the chunks should pick the terrain");

		// The debug view: the levels in their colours, the seams between them bright
		scene.World.Get<SdfTerrainComponent>(scene.Terrain).DebugView = ESdfTerrainDebugView::Seams;
		scene.MakeChunks();
		const RgbaImage seams = scene.Draw();
		SaveIfAsked(seams, "Seams");
		size_t bright = 0;
		for (size_t pixel = 0; pixel < seams.Pixels.size(); pixel += 4)
		{
			bright += seams.Pixels[pixel] > 150 && seams.Pixels[pixel + 2] > 120 && seams.Pixels[pixel + 1] < 100 ? 1 : 0;
		}
		Require(CountChanged(drawn, seams) > covered / 2, "The debug view should colour the terrain anew");
		Require(bright > 50, "The debug view should show the seams bright, not " + std::to_string(bright) + " pixels");

		scene.World.Get<TransformComponent>(scene.Terrain).Position.x += 400.0f;
		const RgbaImage moved = scene.Draw();
		Require(CountChanged(sky, moved) < covered / 4, "The terrain moved away should no longer cover the view");
	}

	// Sky occlusion: inside a closed chamber under flat ground, lit by the sky alone, the picture with
	// it is next to black, without it lit; over open ground, much the same either way
	void TestSkyOcclusion(SceneRenderer& gpu)
	{
		SdfTerrainComponent ground;
		ground.ViewDistance = 160.0f;
		ground.Layers[0].Height = 0.0f;
		ground.Layers[0].Amplitude = 0.0f;
		SdfTerrainEdit chamber;
		chamber.Shape = ESdfTerrainEditShape::Box;
		chamber.Position = glm::vec3(0.0f, -14.0f, 0.0f);
		chamber.HalfSize = glm::vec3(12.0f, 4.0f, 12.0f);
		AppendTerrainEdit(ground, chamber);
		TerrainScene scene(gpu, ground);
		gpu.DrawUntilShadersCompiled();

		auto both = [&](const glm::vec3& position, const glm::vec3& at, const std::string& name)
		{
			scene.Look(position, at);
			scene.World.Get<SdfTerrainComponent>(scene.Terrain).SkyOcclusion = 1.0f;
			scene.MakeChunks();
			const RgbaImage with = scene.Draw();
			scene.World.Get<SdfTerrainComponent>(scene.Terrain).SkyOcclusion = 0.0f;
			scene.MakeChunks();
			const RgbaImage without = scene.Draw();
			SaveIfAsked(with, name + "-Occluded");
			SaveIfAsked(without, name + "-Open");
			return std::pair(MeanBrightness(with), MeanBrightness(without));
		};
		const auto [inside, insideOpen] = both(glm::vec3(-8.0f, -15.0f, -8.0f), glm::vec3(8.0f, -17.0f, 8.0f), "Chamber");
		const auto [over, overOpen] = both(glm::vec3(60.0f, 8.0f, 60.0f), glm::vec3(80.0f, 0.0f, 80.0f), "Ground");
		std::cout << "Sky occlusion: a closed chamber " << inside << " of " << insideOpen << " bright, open ground " << over << " of " << overOpen << "\n";
		Require(insideOpen > 10.0 && inside < insideOpen * 0.2, "A closed chamber should be next to black with sky occlusion");
		Require(over > overOpen * 0.9, "Open ground should be much the same with sky occlusion");
	}

	// The height fog in a closed chamber: lit as the camera there is, by next to none of the sky, so
	// as dark as its walls with sky occlusion, glowing without
	void TestFogInTheTerrain(SceneRenderer& gpu)
	{
		SdfTerrainComponent ground;
		ground.ViewDistance = 160.0f;
		ground.Layers[0].Height = 0.0f;
		ground.Layers[0].Amplitude = 0.0f;
		SdfTerrainEdit chamber;
		chamber.Shape = ESdfTerrainEditShape::Box;
		chamber.Position = glm::vec3(0.0f, -14.0f, 0.0f);
		chamber.HalfSize = glm::vec3(12.0f, 4.0f, 12.0f);
		AppendTerrainEdit(ground, chamber);
		TerrainScene scene(gpu, ground);
		EnvironmentComponent air;
		air.bFog = true;
		air.FogDensity = 0.08f;
		air.FogHeightFalloff = 0.0f;
		air.FogHeight = -14.0f;
		scene.World.Add<EnvironmentComponent>(scene.World.CreateEntity(), air);
		gpu.DrawUntilShadersCompiled();

		scene.Look(glm::vec3(-8.0f, -15.0f, -8.0f), glm::vec3(8.0f, -17.0f, 8.0f));
		scene.World.Get<SdfTerrainComponent>(scene.Terrain).SkyOcclusion = 1.0f;
		scene.MakeChunks();
		const RgbaImage with = scene.Draw();
		scene.World.Get<SdfTerrainComponent>(scene.Terrain).SkyOcclusion = 0.0f;
		scene.MakeChunks();
		const RgbaImage without = scene.Draw();
		SaveIfAsked(with, "ChamberFog-Occluded");
		SaveIfAsked(without, "ChamberFog-Open");
		const double inside = MeanBrightness(with);
		const double insideOpen = MeanBrightness(without);
		std::cout << "Fog in a closed chamber: " << inside << " of " << insideOpen << " bright" << std::endl;
		Require(insideOpen > 10.0 && inside < insideOpen * 0.2, "The fog in a closed chamber should be next to black with sky occlusion");
	}
}

	// Far shadows: a slab held 30 m over flat ground, the camera 150 m off, the sun's shadow distance
	// 40 m: the ground under the slab in shadow with them, lit without
	void TestFarShadows(SceneRenderer& gpu)
	{
		SdfTerrainComponent ground;
		ground.ViewDistance = 400.0f;
		ground.Layers[0].Height = 0.0f;
		ground.Layers[0].Amplitude = 0.0f;
		ground.SkyOcclusion = 0.0f;
		SdfTerrainEdit slab;
		slab.Shape = ESdfTerrainEditShape::Box;
		slab.Mode = ESdfTerrainEditMode::Add;
		slab.Position = glm::vec3(0.0f, 30.0f, 0.0f);
		slab.HalfSize = glm::vec3(30.0f, 3.0f, 30.0f);
		AppendTerrainEdit(ground, slab);
		TerrainScene scene(gpu, ground);
		scene.World.Each<DirectionalLightComponent>([](Entity, DirectionalLightComponent& sun) { sun.ShadowDistance = 40.0f; });
		gpu.DrawUntilShadersCompiled();
		scene.Look(glm::vec3(-150.0f, 60.0f, -60.0f), glm::vec3(0.0f, 0.0f, 0.0f));

		scene.World.Get<SdfTerrainComponent>(scene.Terrain).bFarShadows = true;
		scene.MakeChunks();
		const RgbaImage with = scene.Draw();
		scene.World.Get<SdfTerrainComponent>(scene.Terrain).bFarShadows = false;
		scene.MakeChunks();
		const RgbaImage without = scene.Draw();
		SaveIfAsked(with, "FarShadows");
		SaveIfAsked(without, "NoFarShadows");
		const size_t darker = [&]
		{
			size_t count = 0;
			for (size_t pixel = 0; pixel < with.Pixels.size(); pixel += 4)
			{
				const int a = with.Pixels[pixel] + with.Pixels[pixel + 1] + with.Pixels[pixel + 2];
				const int b = without.Pixels[pixel] + without.Pixels[pixel + 1] + without.Pixels[pixel + 2];
				count += a < b * 3 / 4 ? 1 : 0;
			}
			return count;
		}();
		std::cout << "Far shadows: " << darker << " pixels in the slab's shadow past the shadow distance\n";
		Require(darker > 2000, "The slab should shade the ground past the sun's shadow distance, not " + std::to_string(darker) + " pixels");
	}

	// Gate 3's region from high above, with level blending and without: the sliding makes no part
	// of the ground much darker, as triangles slid onto the wrong surface or turned over would
	void TestBlendingFromAfar(SceneRenderer& gpu)
	{
		SdfTerrainComponent region = TerrainGate3::MakeRegion();
		// Another scene's terrain, its first with a Plan layer, to look at it the same way
		if (const char* other = std::getenv("NYX_FAR_SCENE"))
		{
			Registry loaded;
			Require(SceneSerializer::LoadFromFile(other, loaded), std::string("Can't load ") + other);
			loaded.Each<SdfTerrainComponent>([&](Entity, SdfTerrainComponent& found) { region = found; });
			std::cout << "The terrain of " << other << ": seed " << region.Seed << ", " << region.Layers.size() << " layers, " << region.Plan.Pins.size() << " pins, "
					  << region.Plan.Overrides.size() << " overrides, " << region.Edits.size() << " edits, Finest Level " << region.FinestLevel << ", Level Distance "
					  << region.LevelDistance << ", View Distance " << region.ViewDistance << "\n";
		}
		region.ViewDistance = std::max(region.ViewDistance, 3000.0f);
		if (const char* levels = std::getenv("NYX_FAR_LEVEL_DISTANCE"))
		{
			region.LevelDistance = float(std::atof(levels));
		}
		TerrainScene scene(gpu, region);
		gpu.DrawUntilShadersCompiled();
		const char* at = std::getenv("NYX_FAR_VIEW");
		glm::vec3 eye(0.0f, 350.0f, 500.0f);
		glm::vec3 target(0.0f, 0.0f, 0.0f);
		if (at != nullptr)
		{
			std::sscanf(at, "%f,%f,%f,%f,%f,%f", &eye.x, &eye.y, &eye.z, &target.x, &target.y, &target.z);
		}
		scene.Look(eye, target);
		auto draw = [&](ESdfTerrainLevelBlending blending)
		{
			scene.World.Get<SdfTerrainComponent>(scene.Terrain).LevelBlending = blending;
			scene.MakeChunks();
			// A fade over
			for (const auto start = std::chrono::steady_clock::now(); !scene.World.Get<TerrainChunksComponent>(scene.Terrain).Fading.empty();)
			{
				UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "A fade should end");
			}
			const auto start = std::chrono::steady_clock::now();
			while (!scene.World.Has<TerrainWaterComponent>(scene.Terrain) || scene.World.Get<TerrainWaterComponent>(scene.Terrain).Made == 0)
			{
				UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The water should be worked out within a minute");
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return scene.Draw();
		};
		const RgbaImage blended = draw(ESdfTerrainLevelBlending::Slide);
		const RgbaImage plain = draw(ESdfTerrainLevelBlending::Off);
		const RgbaImage faded = draw(ESdfTerrainLevelBlending::Fade);
		Require(CountChanged(faded, plain) == 0, "Fading, with no fade under way, should draw what blending off does");
		SaveIfAsked(blended, "BlendingFromAfar");
		SaveIfAsked(plain, "NoBlendingFromAfar");
		Nyx::SceneViewCameraData camera;
		Require(gpu.Renderer.GetSceneViewCameraData(scene.View, camera), "The view's camera");
		size_t darker = 0;
		std::string where;
		for (uint32_t y = 0; y < blended.Height; ++y)
		{
			for (uint32_t x = 0; x < blended.Width; ++x)
			{
				const size_t pixel = (size_t(y) * blended.Width + x) * 4;
				const int a = blended.Pixels[pixel] + blended.Pixels[pixel + 1] + blended.Pixels[pixel + 2];
				const int b = plain.Pixels[pixel] + plain.Pixels[pixel + 1] + plain.Pixels[pixel + 2];
				// Ground round it, green: not an edge against the sky or a rift moving by a pixel
				bool bGround = x > 0 && y > 0 && x + 1 < blended.Width && y + 1 < blended.Height;
				for (int dy = -1; dy <= 1 && bGround; ++dy)
				{
					for (int dx = -1; dx <= 1 && bGround; ++dx)
					{
						const size_t near = (size_t(int(y) + dy) * blended.Width + size_t(int(x) + dx)) * 4;
						bGround = plain.Pixels[near + 1] > plain.Pixels[near + 2] && plain.Pixels[near + 1] > plain.Pixels[near];
					}
				}
				if (bGround && a * 5 < b * 3)
				{
					// Where it is: the ray through it down to the ground
					if (darker < 6)
					{
						const glm::vec2 ndc((float(x) + 0.5f) / float(blended.Width) * 2.0f - 1.0f, (float(y) + 0.5f) / float(blended.Height) * 2.0f - 1.0f);
						const glm::vec4 far = camera.InverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
						const glm::vec3 direction = glm::normalize(glm::vec3(far) / far.w - camera.CameraWorldPos);
						const std::optional<TerrainRayHit> hit = RayCastTerrain(region, camera.CameraWorldPos, direction, 4000.0f);
						if (hit)
						{
							where += " (" + std::to_string(int(hit->Point.x)) + ", " + std::to_string(int(hit->Point.y)) + ", " + std::to_string(int(hit->Point.z)) + ")";
						}
					}
					++darker;
				}
			}
		}
		std::cout << "Blending from afar: sliding, " << darker << " pixels of ground much darker than without it, at" << where << "; fading, the same as off" << "\n";
		// A few: a far chunk blended shows the coarser level's own shape, which differs, nearer
		Require(darker < 40, "Level blending should make no part of the ground much darker from afar, not " + std::to_string(darker) + " pixels");
	}

	// Halfway through a fade the chunks fading in and those fading out between them cover the ground:
	// no more of it missing than settled, nothing much darker
	void TestHalfwayThroughAFade(SceneRenderer& gpu)
	{
		SdfTerrainComponent region = TerrainGate3::MakeRegion();
		region.LevelBlending = ESdfTerrainLevelBlending::Fade;
		region.FadeTime = 30.0f;
		region.ViewDistance = 3000.0f;
		TerrainScene scene(gpu, region);
		gpu.DrawUntilShadersCompiled();
		const glm::vec3 from(0.0f, 350.0f, 500.0f);
		const glm::vec3 to(0.0f, 260.0f, 300.0f);
		scene.Look(from, glm::vec3(0.0f, 0.0f, 0.0f));
		scene.MakeChunks();
		// Its water too, so both pictures have it
		for (const auto start = std::chrono::steady_clock::now();
			 !scene.World.Has<TerrainWaterComponent>(scene.Terrain) || scene.World.Get<TerrainWaterComponent>(scene.Terrain).Made == 0;)
		{
			UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The water should be worked out within a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		scene.Look(to, glm::vec3(0.0f, 0.0f, -200.0f));
		TerrainChunksComponent& chunks = scene.World.Get<TerrainChunksComponent>(scene.Terrain);
		const uint64_t swaps = chunks.Stats.Swaps;
		const auto start = std::chrono::steady_clock::now();
		while (chunks.Stats.Swaps == swaps)
		{
			UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "Moving should change the levels");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		Require(!chunks.Fading.empty(), "A fade should be under way");
		// Halfway: the fade's clock moved on to its middle
		chunks.FadeStart -= double(region.FadeTime) * 0.5;
		UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
		const RgbaImage halfway = scene.Draw();
		const size_t fadingOut = chunks.Fading.size();
		chunks.FadeStart -= double(region.FadeTime);
		UpdateTerrainChunks(scene.World, { &scene.CameraPosition, 1 }, JobSystem::Get());
		Require(chunks.Fading.empty(), "Past its time the fade should be over");
		const RgbaImage settled = scene.Draw();
		SaveIfAsked(halfway, "HalfwayThroughAFade");
		SaveIfAsked(settled, "AfterAFade");
		// Much different: sky where there was ground, or the like
		size_t apart = 0;
		for (size_t pixel = 0; pixel < halfway.Pixels.size(); pixel += 4)
		{
			int difference = 0;
			for (int channel = 0; channel < 3; ++channel)
			{
				difference += std::abs(int(halfway.Pixels[pixel + channel]) - int(settled.Pixels[pixel + channel]));
			}
			apart += difference > 150 ? 1 : 0;
		}
		std::cout << "Halfway through a fade: " << fadingOut << " chunks fading out, " << apart << " pixels much different from after it" << "\n";
		Require(apart * 200 < halfway.Pixels.size() / 4, "Halfway through a fade, the ground should be covered as after it: " + std::to_string(apart) + " pixels much different");
	}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxTerrainChunksGpuTests", 1);
		TestChunksDrawn(gpu);
		TestSkyOcclusion(gpu);
		TestFogInTheTerrain(gpu);
		TestFarShadows(gpu);
		TestBlendingFromAfar(gpu);
		TestHalfwayThroughAFade(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain chunk GPU tests passed\n";
	return 0;
}

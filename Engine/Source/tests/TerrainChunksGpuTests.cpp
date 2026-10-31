// An SDF terrain's chunks on a real GPU: hills with tunnels and a floating landmass, made round the
// camera at three levels of detail, drawn by the terrain's Mesh Renderer, one object a chunk; a
// click on them picks the terrain; the debug view of levels and seams draws them in their colours;
// moved, the terrain is drawn where it went. Its occlusion volume darkens a closed chamber's sky
// light to next to nothing and leaves open ground as it was.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "ImageFile.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "SdfTerrainComponent.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainOcclusion.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
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
				const bool bOcclusion = World.Get<SdfTerrainComponent>(Terrain).SkyOcclusion <= 0.0f ||
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
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxTerrainChunksGpuTests", 1);
		TestChunksDrawn(gpu);
		TestSkyOcclusion(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain chunk GPU tests passed\n";
	return 0;
}

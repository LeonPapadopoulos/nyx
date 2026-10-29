// An SDF terrain's chunks on a real GPU: hills with tunnels and a floating landmass, made round the
// camera, drawn by the terrain's Mesh Renderer, one object a chunk; a click on them picks the
// terrain; moved, the terrain is drawn where it went.
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
#include "TerrainChunks.h"
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
		glm::vec3 CameraPosition{ -10.0f, 45.0f, 60.0f };

		explicit TerrainScene(SceneRenderer& gpu)
			: Gpu(gpu)
		{
			const Entity camera = World.CreateEntity();
			World.Add<TransformComponent>(camera,
				TransformComponent{ .Position = CameraPosition, .Rotation = glm::quat(glm::radians(glm::vec3(-22.0f, -8.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
			World.Add<CameraComponent>(camera, CameraComponent{});
			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-45.0f, 35.0f, 0.0f))) });
			World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

			Terrain = World.CreateEntity();
			World.Add<TransformComponent>(Terrain, TransformComponent{});
			World.Add<SdfTerrainComponent>(Terrain, MakeTerrain());
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

		// Frames until the chunks round the camera are all in
		void MakeChunks()
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(World, { &CameraPosition, 1 }, JobSystem::Get());
				const TerrainChunksStats& stats = World.Get<TerrainChunksComponent>(Terrain).Stats;
				if (stats.Pending == 0 && stats.Resident >= stats.Wanted && stats.Wanted > 0)
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
		std::cout << "Terrain: " << chunks.Stats.Resident << " chunks, " << chunks.Stats.Triangles << " triangles, " << covered << " pixels\n";

		Nyx::IEditorRenderer& renderer = gpu.Renderer;
		renderer.RequestPick(scene.View, ViewWidth / 2, ViewHeight * 3 / 4);
		gpu.DrawFrames(3);
		const Nyx::IEditorRenderer::PickResult picked = renderer.ConsumeLastPickResult(scene.View);
		Require(picked.bHasNewResult && picked.HitEntity == scene.Terrain, "A click on the chunks should pick the terrain");

		scene.World.Get<TransformComponent>(scene.Terrain).Position.x += 400.0f;
		const RgbaImage moved = scene.Draw();
		Require(CountChanged(sky, moved) < covered / 4, "The terrain moved away should no longer cover the view");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxTerrainChunksGpuTests", 1);
		TestChunksDrawn(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain chunk GPU tests passed\n";
	return 0;
}

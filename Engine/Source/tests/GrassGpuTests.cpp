// The grass on a real GPU: blades over an SDF terrain, about as many as the values ask for, drawn in
// the view's picture; none where the slope, the material or the height rule them out; gone where the
// ground is dug away; under an overhang as on top of it; moving in a wind and still without one; and
// nothing made again while the camera stands still.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "GrassFieldComponent.h"
#include "ImageFile.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshData.h"
#include "MeshRendererComponent.h"
#include "RenderTestSupport.h"
#include "RuntimeMeshComponent.h"
#include "ScatterBlockerComponent.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TransformComponent.h"
#include "WindComponent.h"
#include "VulkanRenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Grass-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	// A flat plain at height 0
	SdfTerrainComponent MakePlain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 3;
		terrain.ViewDistance = 256.0f;
		terrain.LevelDistance = 4.0f;
		terrain.Layers.clear();
		SdfTerrainLayer ground;
		ground.Amplitude = 0.0f;
		ground.Frequency = 0.01f;
		terrain.Layers.push_back(ground);
		return terrain;
	}

	struct GrassScene
	{
		SceneRenderer& Gpu;
		Registry World;
		uint64_t View = 0;
		Entity Terrain;
		Entity Camera;
		Entity Meadow;
		glm::vec3 CameraPosition{ 0.0f, 4.0f, 20.0f };

		explicit GrassScene(SceneRenderer& gpu, const SdfTerrainComponent& terrain = MakePlain())
			: Gpu(gpu)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, TransformComponent{});
			World.Add<CameraComponent>(Camera, CameraComponent{});
			Look(CameraPosition, glm::vec3(0.0f, 0.0f, -10.0f));
			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-45.0f, 35.0f, 0.0f))) });
			World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

			Terrain = World.CreateEntity();
			World.Add<TransformComponent>(Terrain, TransformComponent{});
			World.Add<SdfTerrainComponent>(Terrain, terrain);
			MeshRendererComponent meshRenderer;
			meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
			World.Add<MeshRendererComponent>(Terrain, meshRenderer);

			// Grown on its own entity: no surface would mean the entity itself
			Meadow = Terrain;
			World.Add<GrassFieldComponent>(Meadow, GrassFieldComponent{});

			Nyx::IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			View = renderer.CreateSceneView();
			renderer.SetSceneViewCameraMode(View, Nyx::EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
		}

		~GrassScene()
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

		void MakeChunks()
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(World, { &CameraPosition, 1 }, JobSystem::Get());
				const TerrainChunksStats& stats = World.Get<TerrainChunksComponent>(Terrain).Stats;
				if (stats.bSettled && stats.Pending == 0 && stats.Resident > 0)
				{
					return;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The chunks should be made within a minute");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}

		// Enough frames that every tile round the camera is made and its blades counted
		RgbaImage Settle(uint32_t frames = 12)
		{
			Gpu.DrawFrames(frames);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}

		uint32_t Blades() const
		{
			const SceneViewGrass* grass = Gpu.Renderer.GetSceneViewGrass(View);
			Require(grass != nullptr, "The view has its grass");
			return grass->NearBlades + grass->FarBlades;
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

	void TestBladesDrawn(SceneRenderer& gpu)
	{
		GrassScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.MakeChunks();
		scene.World.Get<GrassFieldComponent>(scene.Meadow).bGrass = false;
		const RgbaImage bare = scene.Settle();
		Require(scene.Blades() == 0, "Off, there are no blades");

		scene.World.Get<GrassFieldComponent>(scene.Meadow).bGrass = true;
		const RgbaImage grown = scene.Settle();
		SaveIfAsked(grown, "Plain");
		scene.Look(glm::vec3(2.0f, 1.2f, 12.0f), glm::vec3(0.0f, 0.3f, 0.0f));
		SaveIfAsked(scene.Settle(), "Close");
		// The two looks of a blade's light, and the sun through it
		GrassFieldComponent& look = scene.World.Get<GrassFieldComponent>(scene.Meadow);
		const RgbaImage rounded = scene.Settle(3);
		look.NormalRounding = 0.0f;
		const RgbaImage flat = scene.Settle(3);
		SaveIfAsked(flat, "CloseFlat");
		Require(CountChanged(rounded, flat) > ViewWidth * ViewHeight / 20, "Normal Rounding changes the blades' light");
		look.NormalRounding = 1.0f;
		look.Translucency = 0.0f;
		Require(CountChanged(rounded, scene.Settle(3)) > 500, "Translucency changes the blades' light");
		look.Translucency = 0.6f;
		scene.Look(glm::vec3(0.0f, 4.0f, 20.0f), glm::vec3(0.0f, 0.0f, -10.0f));
		scene.Settle();
		const uint32_t blades = scene.Blades();
		std::cout << "Blades on the plain: " << blades << ", tiles " << gpu.Renderer.GetGrassStats().Tiles << "\n";
		// Looking along the plain, a good share of a field 100 m in range at 60 a square metre near by
		Require(blades > 20'000 && blades < 2'000'000, "About as many blades as the values ask for: " + std::to_string(blades));
		Require(CountChanged(bare, grown) > ViewWidth * ViewHeight / 10, "The blades are in the picture");

		// At half the density, about half of them: the same blades, thinned
		scene.World.Get<GrassFieldComponent>(scene.Meadow).Density = 30.0f;
		scene.Settle();
		const float ratio = static_cast<float>(scene.Blades()) / static_cast<float>(blades);
		Require(ratio > 0.4f && ratio < 0.6f, "Half the density, half the blades: " + std::to_string(ratio));
		scene.World.Get<GrassFieldComponent>(scene.Meadow).Density = 60.0f;
		scene.Settle();
		Require(gpu.Renderer.GetGrassStats().TilesMade == 0, "A camera standing still makes no tile");

		// Above the height limit, none
		scene.World.Get<GrassFieldComponent>(scene.Meadow).HeightRange = glm::vec2(5.0f, 10.0f);
		scene.Settle();
		Require(scene.Blades() == 0, "None outside the height range");
		scene.World.Get<GrassFieldComponent>(scene.Meadow).HeightRange = glm::vec2(-1000.0f, 1000.0f);

		// On no material of the terrain's
		scene.World.Get<GrassFieldComponent>(scene.Meadow).TerrainMaterials = { 7 };
		scene.Settle();
		Require(scene.Blades() == 0, "None on other materials");
		scene.World.Get<GrassFieldComponent>(scene.Meadow).TerrainMaterials = {};
	}

	// More blades than a view's buffer holds at first and more ground points than the tiles' buffer
	// holds at first: both grow, and every blade is still drawn
	void TestGrowsToFit(SceneRenderer& gpu)
	{
		GrassScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.MakeChunks();
		GrassFieldComponent& grass = scene.World.Get<GrassFieldComponent>(scene.Meadow);
		scene.Settle(30);
		const uint32_t blades = scene.Blades();
		grass.FullDensityDistance = 60.0f;
		grass.Range = 150.0f;
		scene.Settle(120);
		const VulkanGrass::Stats stats = gpu.Renderer.GetGrassStats();
		std::cout << "Grown: " << scene.Blades() << " blades, " << stats.Tiles << " tiles, point buffer " << stats.PointBytes / (1024 * 1024) << " MiB, " << stats.TileBytes / (1024 * 1024) << " MiB of it used\n";
		Require(scene.Blades() > blades * 2, "More blades within more of the field, whatever has to grow");
		Require(stats.TilesWaiting == 0, "Every tile is made");
	}

	// Standing still without wind; in a wind the blades move from frame to frame, and lean its way
	void TestWind(SceneRenderer& gpu)
	{
		GrassScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.Look(glm::vec3(0.0f, 1.5f, 10.0f), glm::vec3(0.0f, 0.3f, 0.0f));
		scene.MakeChunks();
		const RgbaImage still = scene.Settle();
		Require(CountChanged(still, scene.Settle(2)) == 0, "Without wind nothing moves");

		WindComponent wind;
		wind.Speed = 8.0f;
		wind.Gustiness = 1.0f;
		const Entity air = scene.World.CreateEntity();
		scene.World.Add<WindComponent>(air, wind);
		const RgbaImage blown = scene.Settle();
		SaveIfAsked(blown, "Wind");
		Require(CountChanged(still, blown) > ViewWidth * ViewHeight / 20, "The wind bends the blades");
		Require(CountChanged(blown, scene.Settle(4)) > ViewWidth * ViewHeight / 50, "They keep moving");

		// Wind Response 0: as without wind again
		scene.World.Get<GrassFieldComponent>(scene.Meadow).WindResponse = 0.0f;
		const RgbaImage held = scene.Settle();
		Require(CountChanged(held, scene.Settle(2)) == 0, "With no response they stand still in it");
	}

	// Dug away: fewer blades at once, and only the tiles reached made again; filled in again, back
	void TestFollowsEdits(SceneRenderer& gpu)
	{
		GrassScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.Look(glm::vec3(0.0f, 30.0f, 0.1f), glm::vec3(0.0f, 0.0f, 0.0f));
		scene.MakeChunks();
		const RgbaImage before = scene.Settle();
		const uint32_t blades = scene.Blades();
		SdfTerrainComponent& terrain = scene.World.Get<SdfTerrainComponent>(scene.Terrain);

		SdfTerrainEdit dig;
		dig.Shape = ESdfTerrainEditShape::Sphere;
		dig.Mode = ESdfTerrainEditMode::Subtract;
		dig.Position = glm::vec3(0.0f, 0.0f, 0.0f);
		dig.Radius = 8.0f;
		AppendTerrainEdit(terrain, dig);
		scene.MakeChunks();
		const RgbaImage dug = scene.Settle();
		SaveIfAsked(dug, "Dug");
		const uint32_t after = scene.Blades();
		std::cout << "Blades before digging: " << blades << ", after: " << after << "\n";
		Require(after < blades, "A pit in the plain takes its blades away");
		Require(CountChanged(before, dug) > 500, "The pit is in the picture");
	}

	// On a mesh that isn't a terrain, cut up into tiles; a blocker keeps it away, and moved it makes
	// only the tiles it reaches again
	void TestMeshAndBlockers(SceneRenderer& gpu)
	{
		GrassScene scene(gpu);
		gpu.DrawUntilShadersCompiled();
		scene.World.Get<GrassFieldComponent>(scene.Meadow).bGrass = false;

		const float half = 150.0f;
		const int cells = 150;
		auto data = std::make_shared<MeshData>();
		for (int row = 0; row <= cells; ++row)
		{
			for (int column = 0; column <= cells; ++column)
			{
				Vertex vertex;
				vertex.Position = glm::vec3(-half + 2.0f * half * column / cells, 40.0f, -half + 2.0f * half * row / cells);
				vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
				vertex.Color = glm::vec3(0.1f, 0.2f, 0.05f);
				data->Vertices.push_back(vertex);
			}
		}
		for (int row = 0; row < cells; ++row)
		{
			for (int column = 0; column < cells; ++column)
			{
				const uint32_t a = row * (cells + 1) + column;
				const uint32_t b = a + 1;
				const uint32_t c = a + cells + 1;
				const uint32_t d = c + 1;
				data->Indices.insert(data->Indices.end(), { a, c, b, b, c, d });
			}
		}
		const Entity floor = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(floor, TransformComponent{});
		RuntimeMeshComponent runtimeMesh;
		SetRuntimeMesh(runtimeMesh, data);
		scene.World.Add<RuntimeMeshComponent>(floor, runtimeMesh);
		MeshRendererComponent meshRenderer;
		meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
		scene.World.Add<MeshRendererComponent>(floor, meshRenderer);
		scene.World.Add<GrassFieldComponent>(floor, GrassFieldComponent{});

		scene.Look(glm::vec3(0.0f, 70.0f, 0.1f), glm::vec3(0.0f, 40.0f, 0.0f));
		for (int wait = 0; wait < 200 && gpu.Renderer.GetGrassStats().Tiles == 0; ++wait)
		{
			gpu.DrawFrames(1);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		scene.Settle();
		const uint32_t blades = scene.Blades();
		std::cout << "Blades on the mesh: " << blades << ", tiles " << gpu.Renderer.GetGrassStats().Tiles << "\n";
		Require(blades > 1000, "A mesh that isn't a terrain grows blades");

		const Entity post = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(post, TransformComponent{ .Position = glm::vec3(0.0f, 40.0f, 0.0f) });
		ScatterBlockerComponent blocker;
		blocker.Shape = EScatterBlockerShape::Circle;
		blocker.Radius = 10.0f;
		blocker.Margin = 0.0f;
		scene.World.Add<ScatterBlockerComponent>(post, blocker);
		scene.Settle();
		const uint32_t blocked = scene.Blades();
		SaveIfAsked(scene.Settle(2), "Blocker");
		std::cout << "Blades with a blocker: " << blocked << "\n";
		Require(blocked < blades, "A blocker keeps the blades away");

		const uint64_t before = gpu.Renderer.GetGrassStats().TilesMadeInAll;
		scene.World.Get<TransformComponent>(post).Position.x = 4.0f;
		scene.Settle();
		const uint64_t made = gpu.Renderer.GetGrassStats().TilesMadeInAll - before;
		std::cout << "Tiles made again for a blocker moved: " << made << "\n";
		Require(made > 0 && made <= 9, "Moved, it makes only the tiles it reaches again");

		scene.World.Get<GrassFieldComponent>(floor).bUseBlockers = false;
		scene.Settle();
		Require(scene.Blades() > blocked, "With Use Blockers off they grow through it");
	}

	// A slab held over the plain: blades under it and on it
	void TestUnderOverhang(SceneRenderer& gpu)
	{
		SdfTerrainComponent terrain = MakePlain();
		SdfTerrainLayer slab;
		slab.Kind = ESdfTerrainLayerKind::Box;
		slab.Position = glm::vec3(0.0f, 8.0f, 0.0f);
		slab.HalfSize = glm::vec3(20.0f, 2.0f, 20.0f);
		terrain.Layers.push_back(slab);
		GrassScene scene(gpu, terrain);
		gpu.DrawUntilShadersCompiled();
		scene.Look(glm::vec3(30.0f, 3.0f, 0.0f), glm::vec3(0.0f, 2.0f, 0.0f));
		scene.MakeChunks();
		scene.Settle();
		SaveIfAsked(scene.Settle(), "Overhang");
		const uint32_t both = scene.Blades();

		// Only those on top of the slab, then only those beneath it
		scene.World.Get<GrassFieldComponent>(scene.Meadow).HeightRange = glm::vec2(5.0f, 20.0f);
		scene.Settle();
		const uint32_t onTop = scene.Blades();
		scene.World.Get<GrassFieldComponent>(scene.Meadow).HeightRange = glm::vec2(-5.0f, 5.0f);
		scene.Settle();
		const uint32_t beneath = scene.Blades();
		std::cout << "Blades on the slab: " << onTop << ", beneath and round it: " << beneath << ", both: " << both << "\n";
		Require(onTop > 0, "Blades grow on top of the slab");
		Require(beneath > 0, "Blades grow beneath it");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxGrassGpuTests");
		TestBladesDrawn(gpu);
		TestGrowsToFit(gpu);
		TestWind(gpu);
		TestFollowsEdits(gpu);
		TestUnderOverhang(gpu);
		TestMeshAndBlockers(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxGrassGpuTests failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxGrassGpuTests passed\n";
	return 0;
}

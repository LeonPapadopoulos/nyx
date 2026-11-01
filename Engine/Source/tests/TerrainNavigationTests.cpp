// An SDF terrain's navigation tiles (TerrainNavigation.h): made round a viewpoint while the chunks
// are; a path across the plains on the ground; round a trench too wide to step over; a dig made
// into the tiles it reaches, the path going round it after; nothing without Navigation.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainNavigation.h"
#include "TerrainPlacement.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 2;
		terrain.Layers.clear();
		SdfTerrainLayer ground;
		ground.Kind = ESdfTerrainLayerKind::Plan;
		ground.Frequency = 0.006f;
		terrain.Layers.push_back(ground);
		terrain.ViewDistance = 128.0f;
		terrain.Navigation.bEnabled = true;
		terrain.Navigation.Distance = 80.0f;
		PrepareTerrainWorldPlan(terrain);
		return terrain;
	}

	// A wide flat place on the plains, from a Place Rule
	glm::vec3 FindFlatPlace(const SdfTerrainComponent& terrain)
	{
		SdfTerrainComponent finding = terrain;
		SdfTerrainPlaceRule flat;
		flat.Name = "Flat";
		flat.Radius = 40.0f;
		flat.MaxSlope = 5.0f;
		flat.Clearance = 6.0f;
		flat.bRoughland = false;
		flat.bTunnels = false;
		finding.PlaceRules = { flat };
		const TerrainPlacement placement = PlaceOnTerrain(finding, &JobSystem::Get());
		Require(!placement.Places.empty(), "The test world should have a wide flat place");
		return placement.Places.front().Position;
	}

	float PathLength(const std::vector<glm::vec3>& path)
	{
		float length = 0.0f;
		for (size_t n = 1; n < path.size(); ++n)
		{
			length += glm::length(path[n] - path[n - 1]);
		}
		return length;
	}

	struct World
	{
		Registry Registry;
		Entity Terrain;
		glm::vec3 Viewpoint{ 0.0f };

		void Settle()
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(Registry, { &Viewpoint, 1 }, JobSystem::Get());
				if (Registry.Has<TerrainNavigationComponent>(Terrain) && IsTerrainNavigationSettled(Registry.Get<TerrainNavigationComponent>(Terrain)))
				{
					return;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(120), "The navigation tiles should be made within two minutes");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}

		const TerrainNavigationComponent& Navigation() const { return Registry.Get<TerrainNavigationComponent>(Terrain); }
	};

	void TestPaths()
	{
		World world;
		SdfTerrainComponent values = MakeTerrain();
		const glm::vec3 flat = FindFlatPlace(values);
		world.Terrain = world.Registry.CreateEntity();
		world.Registry.Add<TransformComponent>(world.Terrain, TransformComponent{});
		world.Registry.Add<SdfTerrainComponent>(world.Terrain, values);
		world.Viewpoint = flat + glm::vec3(0.0f, 2.0f, 0.0f);
		const auto start = std::chrono::steady_clock::now();
		world.Settle();
		const TerrainNavigationComponent& navigation = world.Navigation();
		{
			const TerrainNavigationTile tile = MakeTerrainNavigationTile(values, values.Edits, glm::ivec2(glm::floor(glm::vec2(flat.x, flat.z) / TerrainNavigationTileSize)), nullptr);
			std::cout << "A tile alone: " << tile.Milliseconds << " ms, the field " << tile.SampleMilliseconds << " ms in " << tile.Slabs << " slabs, " << tile.Triangles << " triangles, "
					  << tile.Polygons << " polygons, " << tile.Data.size() << " bytes" << std::endl;
		}
		std::cout << "Navigation: " << navigation.Tiles << " tiles, " << navigation.Polygons << " polygons in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
				  << " s, a tile in " << navigation.LastMilliseconds << " ms on its worker\n";
		Require(navigation.Tiles >= 20, "The tiles round the viewpoint should be in");

		// Across the flat place, on the ground
		const glm::vec3 from = flat + glm::vec3(-30.0f, 0.0f, 0.0f);
		const glm::vec3 to = flat + glm::vec3(30.0f, 0.0f, 0.0f);
		const std::optional<std::vector<glm::vec3>> across = FindTerrainNavigationPath(navigation, from, to);
		Require(across && across->size() >= 2, "There should be a way across the flat place");
		Require(glm::length(across->front() - from) < 2.0f && glm::length(across->back() - to) < 2.0f, "The way should start and end where asked");
		Require(PathLength(*across) < 66.0f, "Across flat ground the way should be about straight, not " + std::to_string(PathLength(*across)) + " m");
		for (const glm::vec3& corner : *across)
		{
			const std::optional<TerrainRayHit> ground = RayCastTerrain(world.Registry.Get<SdfTerrainComponent>(world.Terrain), corner + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
			Require(ground && std::fabs(ground->Point.y - corner.y) < 0.6f, "The way should be on the ground");
		}

		// A dig across it: made into the tiles it reaches, the way round it after
		SdfTerrainEdit pit;
		pit.Shape = ESdfTerrainEditShape::Box;
		pit.Position = flat + glm::vec3(0.0f, -6.0f, 0.0f);
		pit.HalfSize = glm::vec3(3.0f, 10.0f, 12.0f);
		const uint64_t made = navigation.Made;
		AppendTerrainEdit(world.Registry.Get<SdfTerrainComponent>(world.Terrain), pit);
		world.Settle();
		const uint64_t remade = navigation.Made - made;
		const std::optional<std::vector<glm::vec3>> round = FindTerrainNavigationPath(navigation, from, to);
		std::cout << "A dig: " << remade << " tiles made again, the way " << (round ? PathLength(*round) : 0.0f) << " m\n";
		// Its box and the tiles' borders: up to two tiles each way across a tile's edge
		Require(remade > 0 && remade <= 6, "A dig should make only the tiles it reaches again, not " + std::to_string(remade));
		// Round its end, 12 m off the straight way 60 m long: about 65 m
		Require(round && PathLength(*round) > 62.0f, "The way should go round the dig, not " + std::to_string(round ? PathLength(*round) : 0.0f) + " m");
		for (const glm::vec3& corner : *round)
		{
			Require(std::fabs(corner.x - flat.x) > 2.5f || std::fabs(corner.z - flat.z) > 11.5f, "No corner of the way should be in the dig");
		}
		Require(FindTerrainNavigationPoint(navigation, flat + glm::vec3(0.0f, -10.0f, 0.0f)) && FindTerrainNavigationPoint(navigation, flat + glm::vec3(0.0f, -10.0f, 0.0f))->y < flat.y - 5.0f,
			"The dig's floor should be walkable too");

		// Off: none
		world.Registry.Get<SdfTerrainComponent>(world.Terrain).Navigation.bEnabled = false;
		UpdateTerrainChunks(world.Registry, { &world.Viewpoint, 1 }, JobSystem::Get());
		Require(!world.Registry.Has<TerrainNavigationComponent>(world.Terrain), "Without Navigation there should be no tiles");
		JobSystem::Get().Wait(JobGroup{});
	}

	// A trench too wide to step over and too deep to climb: the way goes round its end
	void TestRoundATrench()
	{
		SdfTerrainComponent values = MakeTerrain();
		const glm::vec3 flat = FindFlatPlace(values);
		SdfTerrainLayer trench;
		trench.Kind = ESdfTerrainLayerKind::Box;
		trench.Combine = ESdfTerrainCombine::Carve;
		trench.Position = flat;
		trench.HalfSize = glm::vec3(2.0f, 40.0f, 25.0f);
		values.Layers.push_back(trench);
		World world;
		world.Terrain = world.Registry.CreateEntity();
		world.Registry.Add<TransformComponent>(world.Terrain, TransformComponent{});
		world.Registry.Add<SdfTerrainComponent>(world.Terrain, values);
		world.Viewpoint = flat + glm::vec3(0.0f, 2.0f, 0.0f);
		world.Settle();
		const std::optional<std::vector<glm::vec3>> round = FindTerrainNavigationPath(world.Navigation(), flat + glm::vec3(-10.0f, 0.0f, 0.0f), flat + glm::vec3(10.0f, 0.0f, 0.0f));
		std::cout << "Round a trench: " << (round ? PathLength(*round) : 0.0f) << " m\n";
		Require(round && PathLength(*round) > 50.0f, "The way should go round the trench's end");
		JobSystem::Get().Wait(JobGroup{});
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestPaths();
		TestRoundATrench();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Terrain navigation test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "Terrain navigation tests passed\n";
	return 0;
}

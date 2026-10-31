// An SDF terrain's water (TerrainWater.h): the same on its own and on the workers; the sea only on
// ground below its level, pouring off where it reaches none; lakes filled above their ground; no
// river over air; a rift dug across a river makes it fall there and run on from below, the same as
// working everything out again; a lake whose rim is cut drops; the water kept up to date while the
// chunks are, made again after an edit.
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;
	using EBody = TerrainWater::EBody;

	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 2;
		terrain.Layers.clear();
		SdfTerrainLayer ground;
		ground.Kind = ESdfTerrainLayerKind::Plan;
		ground.Frequency = 0.006f;
		terrain.Layers.push_back(ground);
		PrepareTerrainWorldPlan(terrain);
		return terrain;
	}

	bool SameWater(const TerrainWater& a, const TerrainWater& b)
	{
		return a.Cells == b.Cells && std::memcmp(a.Ground.data(), b.Ground.data(), a.Ground.size() * sizeof(float)) == 0 && a.Body == b.Body
			&& std::memcmp(a.Level.data(), b.Level.data(), a.Level.size() * sizeof(float)) == 0 && a.Rivers.size() == b.Rivers.size() && a.Falls.size() == b.Falls.size();
	}

	// Every river's water has ground under it, no farther down than its bed was cut, or a fall at it
	void RequireNoRiverOverAir(const SdfTerrainComponent& terrain, const TerrainWater& water)
	{
		for (const TerrainWater::River& river : water.Rivers)
		{
			for (const glm::vec3& point : river.Points)
			{
				const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, point + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 400.0f);
				bool bFalls = false;
				for (const TerrainWater::Fall& fall : water.Falls)
				{
					bFalls = bFalls || glm::length(glm::vec2(fall.Top.x - point.x, fall.Top.z - point.z)) < TerrainWater::CellSize * 2.0f;
				}
				Require(bFalls || (hit && hit->Point.y > point.y - 12.0f), "A river's water should never be over air");
			}
		}
	}

	void TestTheWater(TerrainWater& outWater)
	{
		const SdfTerrainComponent terrain = MakeTerrain();
		const auto start = std::chrono::steady_clock::now();
		const TerrainWater alone = MakeTerrainWater(terrain);
		const double alonetime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const auto workersStart = std::chrono::steady_clock::now();
		const TerrainWater onWorkers = MakeTerrainWater(terrain, nullptr, nullptr, nullptr, &JobSystem::Get());
		const double workersTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - workersStart).count();
		Require(SameWater(alone, onWorkers), "The water should be the same worked out alone or on the workers");

		const SdfTerrainWorldPlan& made = terrain.Plan;
		size_t sea = 0;
		size_t lake = 0;
		for (size_t index = 0; index < alone.Body.size(); ++index)
		{
			if (alone.Body[index] == EBody::None)
			{
				continue;
			}
			Require(alone.Ground[index] > TerrainWater::NoGround * 0.5f, "Water should stand on ground");
			Require(alone.Ground[index] < alone.Level[index], "Water should stand above its ground");
			sea += alone.Body[index] == EBody::Sea ? 1 : 0;
			lake += alone.Body[index] == EBody::Lake ? 1 : 0;
			if (alone.Body[index] == EBody::Sea)
			{
				Require(alone.Level[index] == made.SeaLevel, "The sea should be at its level");
			}
		}
		Require(sea > 1000, "There should be sea");
		Require(lake > 20, "There should be a lake");
		Require(!alone.Falls.empty(), "The sea should pour off the edge somewhere");
		Require(!alone.Rivers.empty(), "There should be rivers");
		RequireNoRiverOverAir(terrain, alone);
		std::cout << "The water: " << sea << " cells of sea, " << lake << " of lakes, " << alone.Rivers.size() << " river stretches, " << alone.Falls.size() << " falls; "
				  << alonetime << " ms alone, " << workersTime << " ms on " << JobSystem::Get().GetWorkerCount() << " workers\n";
		const MeshData mesh = MakeTerrainWaterMesh(alone);
		Require(mesh.Indices.size() % 3 == 0 && mesh.Vertices.size() > 1000, "The water should be drawn");
		outWater = alone;
	}

	// A rift dug across a river's middle: the river falls there, nothing over air, and keeping the ground
	// outside the dig gives the same as working it all out again
	void TestRiftAcrossARiver(const TerrainWater& before)
	{
		SdfTerrainComponent terrain = MakeTerrain();
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		const TerrainWorldPlan::River& river = plan->Rivers.front();
		const size_t middle = river.Points.size() / 3;
		const glm::vec2 at = river.Points[middle];
		const glm::vec2 way = glm::normalize(river.Points[middle + 2] - river.Points[middle]);
		SdfTerrainEdit dig;
		dig.Shape = ESdfTerrainEditShape::Box;
		dig.Mode = ESdfTerrainEditMode::Subtract;
		dig.Position = glm::vec3(at.x, river.Beds[middle] - 20.0f, at.y);
		dig.HalfSize = glm::vec3(10.0f, 30.0f, 40.0f);
		// Its long side across the river
		const float degrees = glm::degrees(std::atan2(way.x, way.y));
		dig.Rotation = glm::vec3(0.0f, degrees + 90.0f, 0.0f);
		AppendTerrainEdit(terrain, dig);

		glm::vec3 low;
		glm::vec3 high;
		GetTerrainEditBounds(terrain.Edits.back(), low, high);
		const TerrainWater kept = MakeTerrainWater(terrain, &before, &low, &high, &JobSystem::Get());
		const TerrainWater whole = MakeTerrainWater(terrain);
		Require(SameWater(kept, whole), "Keeping the ground outside the dig should give the same water as working it all out");
		bool bFalls = false;
		for (const TerrainWater::Fall& fall : whole.Falls)
		{
			bFalls = bFalls || glm::length(glm::vec2(fall.Top.x, fall.Top.z) - at) < 40.0f;
		}
		Require(bFalls, "The river should fall into the rift dug across it");
		RequireNoRiverOverAir(terrain, whole);
		std::cout << "A rift across a river: " << whole.Rivers.size() << " stretches, " << whole.Falls.size() << " falls\n";
	}

	// A trench from a lake's middle out past its shore, deeper than its water: it drops
	void TestLakeDrains(const TerrainWater& before)
	{
		SdfTerrainComponent terrain = MakeTerrain();
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		const TerrainWorldPlan::Lake& lake = plan->Lakes.front();
		const glm::ivec2 cell = before.CellOf(lake.Center);
		float level = 0.0f;
		for (size_t index = 0; index < before.Body.size(); ++index)
		{
			if (before.Body[index] == EBody::Lake && glm::length(before.PointOf(int(index % size_t(before.Cells)), int(index / size_t(before.Cells))) - lake.Center) < 60.0f)
			{
				level = before.Level[index];
			}
		}
		Require(level != 0.0f, "The lake should have water at its middle");
		const glm::vec2 out = glm::normalize(lake.Center) * -1.0f;
		SdfTerrainEdit trench;
		trench.Shape = ESdfTerrainEditShape::Capsule;
		trench.Mode = ESdfTerrainEditMode::Subtract;
		trench.Position = glm::vec3(lake.Center.x, level - 6.0f, lake.Center.y);
		trench.End = trench.Position + glm::vec3(out.x, 0.0f, out.y) * 600.0f;
		trench.Radius = 8.0f;
		AppendTerrainEdit(terrain, trench);
		const TerrainWater after = MakeTerrainWater(terrain);
		const size_t index = after.IndexOf(cell.x, cell.y);
		const float now = after.Body[index] == EBody::Lake ? after.Level[index] : -1.0e9f;
		std::cout << "A lake's rim cut: from " << level << " m to " << (now > -1.0e8f ? std::to_string(now) + " m" : std::string("dry")) << "\n";
		Require(now < level - 2.0f, "A lake whose rim is cut should drop");
	}

	// While the chunks are kept up to date, the water is too: made, then made again after an edit
	void TestKeptUpToDate()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<SdfTerrainComponent>(entity, MakeTerrain());
		world.Get<SdfTerrainComponent>(entity).ViewDistance = 256.0f;
		const glm::vec3 viewpoint(0.0f, 100.0f, 0.0f);
		auto until = [&](uint32_t made)
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
				if (world.Has<TerrainWaterComponent>(entity) && world.Get<TerrainWaterComponent>(entity).Made >= made)
				{
					return;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The water should be worked out within a minute");
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		until(1);
		const TerrainWaterComponent& water = world.Get<TerrainWaterComponent>(entity);
		Require(water.Mesh.Mesh && !water.Mesh.Mesh->Vertices.empty(), "The water should have a mesh to draw");
		SdfTerrainEdit dig;
		dig.Position = glm::vec3(0.0f, 20.0f, 0.0f);
		dig.Radius = 12.0f;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(entity), dig);
		until(2);
		std::cout << "Kept up to date: made again after an edit in " << world.Get<TerrainWaterComponent>(entity).LastMilliseconds << " ms\n";
		world.Get<SdfTerrainComponent>(entity).Layers[0].bEnabled = false;
		UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
		Require(!world.Has<TerrainWaterComponent>(entity), "Without a Plan layer there should be no water");
		JobSystem::Get().Wait(JobGroup{});
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		TerrainWater water;
		TestTheWater(water);
		TestRiftAcrossARiver(water);
		TestLakeDrains(water);
		TestKeptUpToDate();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Terrain water test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "Terrain water tests passed\n";
	return 0;
}

// An SDF terrain's traversal graph (TerrainTraversal.h): the same alone and on the workers; every
// floor on the ground with room over it, tunnels' floors under the top; every edge's tag as its
// heights ask; a route made of edges, walking round a trench a jump crosses; what each floor needs to
// be reached; separate approaches counted over 16 m cells on graphs made by hand (a walled square
// with one way in, then two, and open ground up to the most asked); the Route Checks of the world's
// places, and kept up to date while the chunks are.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainPlacement.h"
#include "TerrainTraversal.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
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
		SdfTerrainPlaceRule wide;
		wide.Name = "Wide";
		wide.Count = 3;
		wide.Radius = 24.0f;
		wide.MaxSlope = 6.0f;
		wide.Clearance = 10.0f;
		wide.Spacing = 300.0f;
		wide.bRoughland = false;
		wide.bTunnels = false;
		terrain.PlaceRules = { wide };
		PrepareTerrainWorldPlan(terrain);
		return terrain;
	}

	bool SameGraph(const TerrainTraversal& a, const TerrainTraversal& b)
	{
		return a.Columns == b.Columns && a.ColumnFirst == b.ColumnFirst && a.FloorHeights.size() == b.FloorHeights.size()
			&& std::memcmp(a.FloorHeights.data(), b.FloorHeights.data(), a.FloorHeights.size() * sizeof(float)) == 0 && a.Wet == b.Wet && a.EdgeFirst == b.EdgeFirst
			&& a.EdgeTargets == b.EdgeTargets && a.EdgeTags == b.EdgeTags;
	}

	void TestTheGraph()
	{
		const SdfTerrainComponent terrain = MakeTerrain();
		const TerrainWater water = MakeTerrainWater(terrain, nullptr, nullptr, nullptr, &JobSystem::Get());
		const TerrainTraversal traversal = BuildTerrainTraversal(terrain, &water, &JobSystem::Get());
		std::cout << "Graph: " << traversal.Columns << " x " << traversal.Columns << " columns, " << traversal.Floors() << " floors, " << traversal.EdgeTargets.size()
				  << " edges in " << traversal.Milliseconds << " ms on the workers\n";
		Require(traversal.Floors() > size_t(traversal.Columns) * size_t(traversal.Columns) / 4, "Most of the landmass should have floors");

		// Alone, the same bits; a part of the rows checked to keep it quick
		const TerrainTraversal alone = BuildTerrainTraversal(terrain, &water);
		std::cout << "  alone in " << alone.Milliseconds << " ms\n";
		Require(SameGraph(alone, traversal), "The graph should be the same built alone and on the workers");

		// Floors on the ground with room over them; some under others (tunnels, rift ledges); some wet
		size_t under = 0;
		size_t wet = 0;
		for (uint32_t floor = 0; floor < uint32_t(traversal.Floors()); floor += 97)
		{
			const glm::vec3 place = traversal.PlaceOf(floor);
			Require(SampleTerrainValue(terrain, place - glm::vec3(0.0f, 0.1f, 0.0f)) <= 0.0f && SampleTerrainValue(terrain, place + glm::vec3(0.0f, 0.1f, 0.0f)) > 0.0f,
				"A floor should be on the ground");
			for (float up = 0.2f; up < traversal.Values.Headroom; up += 0.2f)
			{
				Require(SampleTerrainValue(terrain, place + glm::vec3(0.0f, up, 0.0f)) > -0.05f, "A floor should have its headroom over it");
			}
		}
		for (size_t column = 0; column + 1 < traversal.ColumnFirst.size(); ++column)
		{
			under += traversal.ColumnFirst[column + 1] - traversal.ColumnFirst[column] > 1 ? 1 : 0;
		}
		for (const uint8_t flag : traversal.Wet)
		{
			wet += flag;
		}
		std::cout << "  " << under << " columns with a floor under another, " << wet << " floors under water\n";
		Require(under > 100, "Tunnels should give columns floors under their top");
		Require(wet > 100, "The sea and lakes should make floors wet");

		// Each edge's tag as its heights ask
		const SdfTerrainTraversal& values = traversal.Values;
		const float walkSlope = std::tan(glm::radians(values.WalkSlope));
		for (uint32_t floor = 0; floor < uint32_t(traversal.Floors()); floor += 13)
		{
			for (uint32_t edge = traversal.EdgeFirst[floor]; edge < traversal.EdgeFirst[floor + 1]; ++edge)
			{
				const glm::vec3 a = traversal.PlaceOf(floor);
				const glm::vec3 b = traversal.PlaceOf(traversal.EdgeTargets[edge]);
				const float across = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
				const float rise = b.y - a.y;
				switch (traversal.EdgeTags[edge])
				{
				case TerrainWalk:
					Require(std::fabs(rise) <= values.StepHeight + across * walkSlope + 1e-3f || (rise < 0.0f && -rise <= values.DropHeight), "A walk should be no steeper than walking");
					break;
				case TerrainClimb:
					Require(rise > 0.0f && rise <= values.ClimbHeight, "A climb should go up, no higher than climbing");
					break;
				case TerrainGap:
					Require(across <= values.GapWidth + 1e-3f && std::fabs(rise) <= values.StepHeight + 1.0f, "A gap should be a jump across");
					break;
				case TerrainSwim:
					Require(traversal.Wet[floor] != 0 || traversal.Wet[traversal.EdgeTargets[edge]] != 0, "A swim should be through water");
					break;
				default:
					Require(false, "An edge should have one tag");
				}
			}
		}

		// A route between the wide places, made of edges; what it needs, reached
		const TerrainPlacement placement = PlaceOnTerrain(terrain, &JobSystem::Get());
		Require(placement.Places.size() == 3, "The test world should have its three wide places");
		const std::optional<uint32_t> from = FindTerrainFloor(traversal, placement.Places[0].Position);
		const std::optional<uint32_t> to = FindTerrainFloor(traversal, placement.Places[1].Position);
		Require(from && to, "The places should be on floors");
		const std::optional<TerrainRoute> route = FindTerrainRoute(traversal, *from, *to, TerrainAllTags);
		Require(route && route->Floors.front() == *from && route->Floors.back() == *to, "There should be a route between the places");
		for (size_t step = 0; step + 1 < route->Floors.size(); ++step)
		{
			bool bEdge = false;
			for (uint32_t edge = traversal.EdgeFirst[route->Floors[step]]; edge < traversal.EdgeFirst[route->Floors[step] + 1]; ++edge)
			{
				bEdge = bEdge || traversal.EdgeTargets[edge] == route->Floors[step + 1];
			}
			Require(bEdge, "A route should be made of edges");
		}
		std::cout << "  a route of " << route->Floors.size() << " floors, " << route->Length << " m by its costs, tags " << int(route->Tags) << "\n";
		const std::vector<uint8_t> reach = GetTerrainReach(traversal, *from);
		Require(reach[*to] != 0, "What a route reaches should be reached");
		const std::optional<TerrainRoute> walking = FindTerrainRoute(traversal, *from, *to, TerrainWalk);
		Require((reach[*to] == 1) == walking.has_value(), "A floor reached by walking alone should have a walking route");
	}

	// A trench too deep to drop into, narrower than a jump: walking goes round it, a jump across
	void TestAGap()
	{
		SdfTerrainComponent terrain = MakeTerrain();
		const TerrainPlacement placement = PlaceOnTerrain(terrain, &JobSystem::Get());
		const glm::vec3 middle = placement.Places[0].Position;
		SdfTerrainLayer trench;
		trench.Kind = ESdfTerrainLayerKind::Box;
		trench.Combine = ESdfTerrainCombine::Carve;
		trench.Position = middle;
		trench.HalfSize = glm::vec3(2.0f, 40.0f, 60.0f);
		terrain.Layers.push_back(trench);
		// Columns 2 m apart, so the trench has one or two in it, a jump four columns
		terrain.Traversal.Spacing = 2.0f;
		terrain.Traversal.GapWidth = 8.0f;
		const TerrainTraversal traversal = BuildTerrainTraversal(terrain, nullptr, &JobSystem::Get());
		const std::optional<uint32_t> west = FindTerrainFloor(traversal, middle + glm::vec3(-10.0f, 0.0f, 0.0f));
		const std::optional<uint32_t> east = FindTerrainFloor(traversal, middle + glm::vec3(10.0f, 0.0f, 0.0f));
		Require(west && east, "Both sides of the trench should have floors");
		const std::optional<TerrainRoute> walking = FindTerrainRoute(traversal, *west, *east, TerrainWalk);
		const std::optional<TerrainRoute> jumping = FindTerrainRoute(traversal, *west, *east, TerrainWalk | TerrainGap);
		Require(walking && jumping, "Both should get across");
		std::cout << "Gap: walking round " << walking->Length << " m, jumping " << jumping->Length << " m\n";
		Require(walking->Length > 100.0f, "Walking should go round the trench");
		Require((jumping->Tags & TerrainGap) != 0 && jumping->Length < 60.0f, "A jump should cross it");
	}

	// A graph made by hand: columns 4 m apart, a floor at 0 where `open` says, walking to every
	// neighbour that has one
	TerrainTraversal MakeGraph(int columns, const std::function<bool(int, int)>& open)
	{
		TerrainTraversal traversal;
		traversal.Columns = columns;
		traversal.ColumnFirst.resize(size_t(columns) * size_t(columns) + 1);
		std::vector<int32_t> floorOf(size_t(columns) * size_t(columns), -1);
		for (int j = 0; j < columns; ++j)
		{
			for (int i = 0; i < columns; ++i)
			{
				traversal.ColumnFirst[traversal.IndexOf(i, j)] = uint32_t(traversal.FloorHeights.size());
				if (open(i, j))
				{
					floorOf[traversal.IndexOf(i, j)] = int32_t(traversal.FloorHeights.size());
					traversal.FloorHeights.push_back(0.0f);
				}
			}
		}
		traversal.ColumnFirst.back() = uint32_t(traversal.FloorHeights.size());
		traversal.Wet.assign(traversal.Floors(), 0);
		for (int j = 0; j < columns; ++j)
		{
			for (int i = 0; i < columns; ++i)
			{
				if (floorOf[traversal.IndexOf(i, j)] < 0)
				{
					continue;
				}
				traversal.EdgeFirst.push_back(uint32_t(traversal.EdgeTargets.size()));
				for (int dj = -1; dj <= 1; ++dj)
				{
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = i + di;
						const int nj = j + dj;
						if ((di != 0 || dj != 0) && ni >= 0 && nj >= 0 && ni < columns && nj < columns && floorOf[traversal.IndexOf(ni, nj)] >= 0)
						{
							traversal.EdgeTargets.push_back(uint32_t(floorOf[traversal.IndexOf(ni, nj)]));
							traversal.EdgeTags.push_back(TerrainWalk);
						}
					}
				}
			}
		}
		traversal.EdgeFirst.push_back(uint32_t(traversal.EdgeTargets.size()));
		return traversal;
	}

	// A walled square round the middle (its walls two columns thick), with ways in through it
	void TestApproaches()
	{
		const int columns = 40;
		auto walled = [](std::vector<glm::ivec2> ways)
		{
			return [ways](int i, int j)
			{
				const bool bWall = ((i == 12 || i == 13 || i == 26 || i == 27) && j >= 12 && j <= 27) || ((j == 12 || j == 13 || j == 26 || j == 27) && i >= 12 && i <= 27);
				for (const glm::ivec2& way : ways)
				{
					if (i == way.x && j == way.y)
					{
						return true;
					}
				}
				return !bWall;
			};
		};
		const glm::vec3 middle(80.0f, 0.0f, 80.0f);
		// One way in: two columns side by side through the left wall, in one 16 m cell
		const TerrainTraversal one = MakeGraph(columns, walled({ { 12, 16 }, { 13, 16 }, { 12, 17 }, { 13, 17 } }));
		const std::optional<uint32_t> outside = FindTerrainFloor(one, glm::vec3(8.0f, 0.0f, 8.0f));
		Require(outside.has_value(), "The hand-made graph should have a floor outside");
		const uint32_t oneWay = CountTerrainApproaches(one, *outside, middle, 8.0f, TerrainWalk, 8);
		// Another, through the right wall
		const TerrainTraversal two = MakeGraph(columns, walled({ { 12, 16 }, { 13, 16 }, { 12, 17 }, { 13, 17 }, { 26, 20 }, { 27, 20 }, { 26, 21 }, { 27, 21 } }));
		const uint32_t twoWays = CountTerrainApproaches(two, *FindTerrainFloor(two, glm::vec3(8.0f, 0.0f, 8.0f)), middle, 8.0f, TerrainWalk, 8);
		// No walls: as many as asked, from a cell with eight round it (a corner's has three)
		const TerrainTraversal open = MakeGraph(columns, [](int, int) { return true; });
		const uint32_t openWays = CountTerrainApproaches(open, *FindTerrainFloor(open, glm::vec3(24.0f, 0.0f, 24.0f)), middle, 8.0f, TerrainWalk, 4);
		const TerrainTraversal shut = MakeGraph(columns, walled({}));
		const uint32_t noWay = CountTerrainApproaches(shut, *FindTerrainFloor(shut, glm::vec3(8.0f, 0.0f, 8.0f)), middle, 8.0f, TerrainWalk, 8);
		std::cout << "Approaches: walled " << noWay << ", one way in " << oneWay << ", two " << twoWays << ", open " << openWays << " of 4 asked\n";
		Require(noWay == 0 && oneWay == 1 && twoWays == 2 && openWays == 4, "The ways in should be counted by the cells they pass");
		const std::vector<uint8_t> reach = GetTerrainReach(shut, *FindTerrainFloor(shut, glm::vec3(8.0f, 0.0f, 8.0f)));
		Require(reach[*FindTerrainFloor(shut, middle)] == 0, "Nothing should reach inside a shut square");
	}

	// The world's Route Checks, kept up to date while the chunks are
	void TestRouteChecks()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		SdfTerrainComponent terrain = MakeTerrain();
		terrain.ViewDistance = 128.0f;
		SdfTerrainRouteCheck walk;
		walk.From = "Wide";
		walk.To = "Wide";
		walk.bSwim = true;
		walk.bClimb = true;
		walk.Approaches = 2;
		SdfTerrainRouteCheck missing;
		missing.From = "Wide";
		missing.To = "Nowhere";
		terrain.RouteChecks = { walk, missing };
		world.Add<SdfTerrainComponent>(entity, terrain);
		const glm::vec3 viewpoint(0.0f, 100.0f, 0.0f);
		const auto start = std::chrono::steady_clock::now();
		while (!world.Has<TerrainTraversalComponent>(entity) || world.Get<TerrainTraversalComponent>(entity).Made == 0)
		{
			UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(120), "The traversal should be built within two minutes");
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		const TerrainTraversalComponent& traversal = world.Get<TerrainTraversalComponent>(entity);
		Require(traversal.Routes.size() == 2, "Each check should have a result");
		std::cout << "Checks: " << traversal.Routes[0].Why << "; " << traversal.Routes[1].Why << "\n";
		Require(traversal.Routes[0].bPassed && traversal.Routes[0].Reached == 3, "The wide places should reach each other by two ways or more");
		Require(!traversal.Routes[1].bPassed, "A check to a rule that isn't there fails");
		world.Get<SdfTerrainComponent>(entity).RouteChecks.clear();
		UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
		Require(!world.Has<TerrainTraversalComponent>(entity), "Without checks there should be no traversal kept");
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
		TestApproaches();
		TestTheGraph();
		TestAGap();
		TestRouteChecks();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Terrain traversal test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "Terrain traversal tests passed\n";
	return 0;
}

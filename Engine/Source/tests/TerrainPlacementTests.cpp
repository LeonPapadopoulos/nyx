// An SDF terrain's Place Rules (TerrainPlacement.h): the same places alone and on the workers and
// every time; each place meeting its rule as the field has the ground (kind, height, out of the
// water, its footprint's slope, the clearance over it, the distances); another rule leaving the
// others' places as they were; a rule that can't be met taking its next seeds, then its Fallback;
// the places kept up to date while the chunks are, never by the edits, and copies of a mesh at them.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainPlacement.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	SdfTerrainPlaceRule MakeRule(const std::string& name, uint32_t count)
	{
		SdfTerrainPlaceRule rule;
		rule.Name = name;
		rule.Count = count;
		rule.Radius = 10.0f;
		rule.MaxSlope = 12.0f;
		rule.Clearance = 8.0f;
		rule.Spacing = 150.0f;
		return rule;
	}

	// The Gate 3 kind of world, with three rules: wide flat places, small ones near them, and a few on
	// rough land
	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 2;
		terrain.Layers.clear();
		SdfTerrainLayer ground;
		ground.Kind = ESdfTerrainLayerKind::Plan;
		ground.Frequency = 0.006f;
		terrain.Layers.push_back(ground);
		SdfTerrainPlaceRule wide = MakeRule("Wide", 4);
		wide.Radius = 20.0f;
		wide.bRoughland = false;
		SdfTerrainPlaceRule near = MakeRule("Near", 6);
		near.Radius = 4.0f;
		near.MaxSlope = 25.0f;
		near.Spacing = 30.0f;
		near.Near = "Wide";
		near.NearDistance = glm::vec2(30.0f, 120.0f);
		SdfTerrainPlaceRule high = MakeRule("High", 3);
		high.bPlains = false;
		high.MaxSlope = 30.0f;
		high.Radius = 6.0f;
		terrain.PlaceRules = { wide, near, high };
		PrepareTerrainWorldPlan(terrain);
		return terrain;
	}

	bool SamePlaces(const TerrainPlacement& a, const TerrainPlacement& b)
	{
		if (a.Places.size() != b.Places.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.Places.size(); ++index)
		{
			const TerrainPlace& x = a.Places[index];
			const TerrainPlace& y = b.Places[index];
			if (x.Rule != y.Rule || std::memcmp(&x.Position, &y.Position, sizeof(glm::vec3)) != 0 || x.Yaw != y.Yaw || x.bFallback != y.bFallback)
			{
				return false;
			}
		}
		return true;
	}

	std::string Describe(const glm::vec3& point)
	{
		return std::to_string(point.x) + ", " + std::to_string(point.y) + ", " + std::to_string(point.z);
	}

	// Each place as its rule asks, checked against the field again on its own
	void RequireMeetsRules(const SdfTerrainComponent& terrain, const TerrainPlacement& placement)
	{
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		for (size_t index = 0; index < placement.Places.size(); ++index)
		{
			const TerrainPlace& place = placement.Places[index];
			const SdfTerrainPlaceRule& rule = terrain.PlaceRules[place.Rule];
			if (place.bFallback)
			{
				continue;
			}
			const std::string where = rule.Name + " at " + Describe(place.Position);
			const glm::vec2 point(place.Position.x, place.Position.z);

			// On its ground, from above
			const std::optional<TerrainRayHit> down = RayCastTerrain(terrain, place.Position + glm::vec3(0.0f, 200.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 400.0f);
			Require(down && std::fabs(down->Point.y - place.Position.y) < 0.05f, where + " should be on the ground from above");
			Require(place.Position.y >= rule.HeightRange.x && place.Position.y <= rule.HeightRange.y, where + " should be within its height range");

			// Its kind of ground, out of the water
			const glm::ivec2 node = plan->NodeOf(point);
			const ESdfTerrainLandform landform = ESdfTerrainLandform(plan->Landform[plan->IndexOf(node.x, node.y)]);
			Require((landform != ESdfTerrainLandform::Plains || rule.bPlains) && (landform != ESdfTerrainLandform::Roughland || rule.bRoughland),
				where + " should be on a kind of ground its rule allows");
			Require(!rule.bDry || place.Position.y > plan->Made.SeaLevel, where + " should be above the sea's level");

			// Its footprint no steeper than asked, nothing over it
			for (int n = 0; n < 16; ++n)
			{
				const float angle = float(n) * 0.3926991f;
				const glm::vec3 edge = place.Position + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * rule.Radius;
				const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, edge + glm::vec3(0.0f, 100.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 200.0f);
				const float rise = std::tan(glm::radians(rule.MaxSlope)) * rule.Radius;
				Require(hit && std::fabs(hit->Point.y - place.Position.y) <= rise * 1.25f + 0.3f,
					where + ": its footprint should be no steeper than its rule's slope, off by " + std::to_string(hit ? hit->Point.y - place.Position.y : 0.0f));
			}
			for (float up = 0.5f; up <= rule.Clearance; up += 0.5f)
			{
				Require(SampleTerrainValue(terrain, place.Position + glm::vec3(0.0f, up, 0.0f)) > 0.0f, where + " should have its clearance free above it");
			}

			// Apart, and near what it must be near
			float nearest = std::numeric_limits<float>::max();
			for (size_t other = 0; other < placement.Places.size(); ++other)
			{
				const TerrainPlace& them = placement.Places[other];
				const float apart = glm::length(glm::vec2(them.Position.x, them.Position.z) - point);
				if (other != index && them.Rule == place.Rule && !them.bFallback)
				{
					Require(apart >= rule.Spacing, where + " should be its rule's spacing from the others");
				}
				if (!rule.Near.empty() && terrain.PlaceRules[them.Rule].Name == rule.Near)
				{
					nearest = std::min(nearest, apart);
				}
			}
			Require(rule.Near.empty() || (nearest >= rule.NearDistance.x && nearest <= rule.NearDistance.y), where + " should be near a " + rule.Near);
		}
	}

	void TestThePlaces()
	{
		const SdfTerrainComponent terrain = MakeTerrain();
		const TerrainPlacement alone = PlaceOnTerrain(terrain);
		const TerrainPlacement workers = PlaceOnTerrain(terrain, &JobSystem::Get());
		std::cout << "Places: " << alone.Places.size() << " in " << alone.Milliseconds << " ms alone, " << workers.Milliseconds << " ms on the workers\n";
		for (size_t index = 0; index < alone.Rules.size(); ++index)
		{
			const TerrainPlacement::RuleResult& result = alone.Rules[index];
			std::cout << "  " << terrain.PlaceRules[index].Name << ": " << result.Found << " found, " << result.Fallbacks << " fallback, " << result.Rolls << " seeds, "
					  << result.Tried << " tried\n";
		}
		Require(SamePlaces(alone, workers), "The places should be the same found alone and on the workers");
		Require(SamePlaces(alone, PlaceOnTerrain(terrain, &JobSystem::Get())), "The places should be the same every time");
		Require(IsTerrainPlacementMet(terrain, alone), "Every rule of the test world should find its places");
		RequireMeetsRules(terrain, alone);

		// Another seed, other places
		SdfTerrainComponent other = terrain;
		other.Seed = 3;
		PrepareTerrainWorldPlan(other);
		Require(!SamePlaces(alone, PlaceOnTerrain(other, &JobSystem::Get())), "Another seed should give other places");

		// A rule added at the end changes no other rule's places
		SdfTerrainComponent more = terrain;
		more.PlaceRules.push_back(MakeRule("More", 2));
		const TerrainPlacement withMore = PlaceOnTerrain(more, &JobSystem::Get());
		TerrainPlacement before = withMore;
		std::erase_if(before.Places, [](const TerrainPlace& place) { return place.Rule == 3; });
		Require(SamePlaces(alone, before), "A rule added should leave the others' places as they were");
	}

	// A rule nothing meets takes its 8 seeds, then its Fallback places, on the ground there
	void TestFallback()
	{
		SdfTerrainComponent terrain = MakeTerrain();
		SdfTerrainPlaceRule impossible = MakeRule("Impossible", 2);
		impossible.HeightRange = glm::vec2(5000.0f, 6000.0f);
		impossible.Fallback = { glm::vec2(10.0f, 20.0f), glm::vec2(-30.0f, 40.0f), glm::vec2(50.0f, 50.0f) };
		terrain.PlaceRules.push_back(impossible);
		const TerrainPlacement placement = PlaceOnTerrain(terrain, &JobSystem::Get());
		const TerrainPlacement::RuleResult& result = placement.Rules.back();
		Require(result.Found == 0 && result.Rolls == 8 && result.Fallbacks == 2, "A rule nothing meets should take 8 seeds, then two of its Fallback places");
		Require(result.Misses[size_t(ETerrainPlaceMiss::Height)] > 0, "Its tries should miss by their height");
		Require(!IsTerrainPlacementMet(terrain, placement), "A rule on its Fallback isn't met");
		uint32_t fallbacks = 0;
		for (const TerrainPlace& place : placement.Places)
		{
			if (place.Rule != 3)
			{
				continue;
			}
			Require(place.bFallback, "Its places should be its Fallback's");
			const glm::vec2 expected = impossible.Fallback[fallbacks++];
			Require(place.Position.x == expected.x && place.Position.z == expected.y, "A Fallback place should be where the rule says");
			const std::optional<TerrainRayHit> down = RayCastTerrain(terrain, place.Position + glm::vec3(0.0f, 200.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 400.0f);
			Require(down && std::fabs(down->Point.y - place.Position.y) < 0.05f, "A Fallback place should be on the ground there");
		}

		// A Near no earlier rule has can't be met either
		SdfTerrainComponent nowhere = MakeTerrain();
		nowhere.PlaceRules[0].Near = "High";
		const TerrainPlacement unmet = PlaceOnTerrain(nowhere, &JobSystem::Get());
		Require(unmet.Rules[0].Found == 0, "A Near that names a later rule can't be met");
	}

	// While the chunks are kept up to date, so are the places: found, never again for an edit, again
	// for another rule; copies of a mesh at a rule's places
	void TestKeptUpToDate()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0x1234 } });
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<SdfTerrainComponent>(entity, MakeTerrain());
		world.Get<SdfTerrainComponent>(entity).ViewDistance = 128.0f;
		TransformComponent moved;
		moved.Position = glm::vec3(5.0f, 1.0f, -2.0f);
		const Entity copies = world.CreateEntity();
		world.Add<TransformComponent>(copies, moved);
		TerrainPlaceCopiesComponent values;
		values.Terrain = EntityGuid{ 0x1234 };
		values.Rule = "Near";
		world.Add<TerrainPlaceCopiesComponent>(copies, values);

		const glm::vec3 viewpoint(0.0f, 100.0f, 0.0f);
		auto until = [&](uint32_t made)
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
				if (world.Has<TerrainPlacesComponent>(entity) && world.Get<TerrainPlacesComponent>(entity).Made >= made)
				{
					UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
					return;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The places should be found within a minute");
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		until(1);
		const TerrainPlacement placement = *world.Get<TerrainPlacesComponent>(entity).Placement;
		Require(SamePlaces(placement, PlaceOnTerrain(world.Get<SdfTerrainComponent>(entity))), "The places kept should be those found on their own");
		Require(world.Has<MeshInstancesComponent>(copies), "The copies should have instances");
		const MeshInstances& instances = *world.Get<MeshInstancesComponent>(copies).Instances;
		Require(instances.Transforms.size() == 6, "There should be a copy at each of Near's six places, not " + std::to_string(instances.Transforms.size()));
		const glm::vec3 first = glm::vec3(instances.Transforms[0][3]) + moved.Position;
		Require(glm::length(first - placement.Places[4].Position) < 1e-3f, "A copy should stand at its place, in its entity's space");

		// An edit changes no place; another rule does
		SdfTerrainEdit dig;
		dig.Position = placement.Places[0].Position;
		dig.Radius = 6.0f;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(entity), dig);
		for (int frame = 0; frame < 20; ++frame)
		{
			UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
		}
		Require(world.Get<TerrainPlacesComponent>(entity).Made == 1, "An edit shouldn't find the places again");
		world.Get<SdfTerrainComponent>(entity).PlaceRules[1].Count = 3;
		until(2);
		Require(world.Get<MeshInstancesComponent>(copies).Instances->Transforms.size() == 3, "Fewer places asked, fewer copies");
		world.Get<SdfTerrainComponent>(entity).PlaceRules.clear();
		UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
		Require(!world.Has<TerrainPlacesComponent>(entity), "Without rules there should be no places");
		Require(world.Get<MeshInstancesComponent>(copies).Instances->Transforms.empty(), "Without places there should be no copies");
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
		TestThePlaces();
		TestFallback();
		TestKeptUpToDate();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Terrain placement test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "Terrain placement tests passed\n";
	return 0;
}

// Batch seed tests (TerrainSeedTests.h): a seed tested finds the places the terrain would with that
// seed, checks its routes, and times its parts and the worst chunk at its places; a rule nothing
// meets fails every seed and is what fails most; without checks nothing is checked; the report
// says how many passed and what failed most.
#include "ComponentRegistration.h"
#include "JobSystem.h"
#include "Log.h"
#include "SdfTerrainComponent.h"
#include "TerrainPlacement.h"
#include "TerrainSeedTests.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
		wide.Count = 2;
		wide.Radius = 16.0f;
		wide.MaxSlope = 8.0f;
		wide.Spacing = 300.0f;
		SdfTerrainPlaceRule near;
		near.Name = "Near";
		near.Count = 2;
		near.Radius = 4.0f;
		near.MaxSlope = 25.0f;
		near.Spacing = 30.0f;
		near.Near = "Wide";
		near.NearDistance = glm::vec2(20.0f, 150.0f);
		terrain.PlaceRules = { wide, near };
		SdfTerrainRouteCheck check;
		check.From = "Wide";
		check.To = "Near";
		check.bClimb = true;
		check.bSwim = true;
		terrain.RouteChecks = { check };
		return terrain;
	}

	void TestSeeds()
	{
		const SdfTerrainComponent terrain = MakeTerrain();
		std::vector<TerrainSeedTest> tests;
		for (uint32_t seed = 2; seed < 5; ++seed)
		{
			tests.push_back(TestTerrainSeed(terrain, seed, {}, &JobSystem::Get()));
			const TerrainSeedTest& test = tests.back();
			std::cout << "Seed " << seed << ": " << (test.bPassed ? "passed" : "failed") << " in " << test.Milliseconds << " ms (places " << test.PlacesMilliseconds
					  << ", water " << test.WaterMilliseconds << ", traversal " << test.TraversalMilliseconds << ", worst chunk " << test.WorstChunkMilliseconds << ")\n";
			for (const std::string& failure : test.Failures)
			{
				std::cout << "  " << failure << "\n";
			}
			Require(test.bChecked && test.Routes.size() == 1, "A seed of a terrain with rules and a check should be checked");
			Require(test.WorstChunkMilliseconds > 0.0, "A seed should time the chunks at its places");
			Require(test.Failures.size() == test.FailureKeys.size() && test.bPassed == test.Failures.empty(), "A seed passes when nothing failed");

			// The places the terrain finds with that seed
			SdfTerrainComponent same = terrain;
			same.Seed = seed;
			PrepareTerrainWorldPlan(same);
			const TerrainPlacement placement = PlaceOnTerrain(same, &JobSystem::Get());
			Require(placement.Places.size() == test.Placement->Places.size(), "A seed tested should find the places the terrain does with it");
			for (size_t index = 0; index < placement.Places.size(); ++index)
			{
				Require(std::memcmp(&placement.Places[index].Position, &test.Placement->Places[index].Position, sizeof(glm::vec3)) == 0, "The same places, to the bit");
			}
		}

		// A rule nothing meets fails every seed and fails most
		SdfTerrainComponent impossible = terrain;
		SdfTerrainPlaceRule high;
		high.Name = "High";
		high.HeightRange = glm::vec2(5000.0f, 6000.0f);
		impossible.PlaceRules.push_back(high);
		TerrainSeedTestOptions quick;
		quick.bChunks = false;
		std::vector<TerrainSeedTest> failing;
		for (uint32_t seed = 2; seed < 4; ++seed)
		{
			failing.push_back(TestTerrainSeed(impossible, seed, quick, &JobSystem::Get()));
			Require(!failing.back().bPassed, "A rule nothing meets should fail the seed");
		}
		const TerrainSeedSummary summary = SummariseTerrainSeeds(failing);
		Require(summary.Seeds == 2 && summary.Checked == 2 && summary.Passed == 0, "No seed should pass");
		// Tried off the landmass and in the sea too, most of its tries miss by the kind of ground first
		Require(!summary.FailingMost.empty() && summary.FailingMost.front().first.starts_with("Place Rule High (") && summary.FailingMost.front().second == 2,
			"The rule nothing meets should fail most");
		const std::string report = WriteTerrainSeedReport(impossible, failing, summary);
		Require(report.find("**0 of 2 passed**") != std::string::npos && report.find(summary.FailingMost.front().first + " | 2") != std::string::npos, "The report should say so");
		std::cout << "Failing most: " << summary.FailingMost.front().first << "\n";

		// Without checks, nothing checked
		TerrainSeedTestOptions plain;
		plain.bChecks = false;
		const TerrainSeedTest unchecked = TestTerrainSeed(terrain, 2, plain, &JobSystem::Get());
		Require(!unchecked.bChecked && !unchecked.Placement && unchecked.WorstChunkMilliseconds > 0.0, "Without checks only the plan and the chunks at spots over the land");
		const TerrainSeedSummary all = SummariseTerrainSeeds(tests);
		std::cout << all.Passed << " of " << all.Checked << " passed; the worst chunk " << all.WorstChunk.Most << " ms, seed " << all.WorstChunkSeed << "\n";
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestSeeds();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Terrain seed test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "Terrain seed tests passed\n";
	return 0;
}

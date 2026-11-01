#pragma once

#include "SdfTerrainComponent.h"
#include "TerrainPlacement.h"
#include "TerrainTraversal.h"
#include "TerrainWorldPlanMap.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Nyx
{
	class JobSystem;

	// What a seed test does besides making the plan
	struct TerrainSeedTestOptions
	{
		// The places found and the Route Checks checked (needs Place Rules)
		bool bChecks = true;
		// The finest chunks at the places (or, without places, at spots spread over the land) made
		// on one thread, the slowest kept: the worst geometry cost at chosen spots
		bool bChunks = true;
		uint32_t MostChunks = 12;
	};

	// One seed of an SDF terrain's world made and checked: its plan and shares, the places its rules
	// found and its Route Checks, what failed, and how long each part took
	struct TerrainSeedTest
	{
		uint32_t Seed = 0;
		TerrainPlanSeed Plan;
		std::shared_ptr<const TerrainPlacement> Placement;
		std::vector<TerrainRouteResult> Routes;
		// Whether its rules were all met and its checks all passed; what failed, a line each, and
		// what each failure is counted as across seeds (the rule's or check's name)
		bool bChecked = false;
		bool bPassed = true;
		std::vector<std::string> Failures;
		std::vector<std::string> FailureKeys;
		// In milliseconds: the water, the places, the traversal graph and checks; the slowest chunk at
		// the spots, where it is; all of it
		double WaterMilliseconds = 0.0;
		double PlacesMilliseconds = 0.0;
		double TraversalMilliseconds = 0.0;
		double WorstChunkMilliseconds = 0.0;
		glm::vec3 WorstChunkAt{ 0.0f };
		double Milliseconds = 0.0;
	};

	// Makes and checks one seed of the terrain's world (the terrain as it is, its seed set to this
	// one): its plan, then with checks its water, places, traversal graph and Route Checks, then the
	// chunks at its places. Parts on `jobs`' workers, if given. A traversal graph is built only for
	// Route Checks.
	TerrainSeedTest TestTerrainSeed(const SdfTerrainComponent& terrain, uint32_t seed, const TerrainSeedTestOptions& options = {}, JobSystem* jobs = nullptr);

	// How many seeds passed, what failed most (by its key, most first), the shares' spread, and the
	// parts' times
	struct TerrainSeedSummary
	{
		uint32_t Seeds = 0;
		uint32_t Checked = 0;
		uint32_t Passed = 0;
		std::vector<std::pair<std::string, uint32_t>> FailingMost;
		TerrainPlanSpread Spread;
		TerrainPlanSpread::Range Water;
		TerrainPlanSpread::Range Places;
		TerrainPlanSpread::Range Traversal;
		TerrainPlanSpread::Range WorstChunk;
		TerrainPlanSpread::Range Total;
		uint32_t WorstChunkSeed = 0;
	};
	TerrainSeedSummary SummariseTerrainSeeds(const std::vector<TerrainSeedTest>& tests);

	// The tests as a report in Markdown: the summary, what failed most, then a line a seed
	std::string WriteTerrainSeedReport(const SdfTerrainComponent& terrain, const std::vector<TerrainSeedTest>& tests, const TerrainSeedSummary& summary);
}

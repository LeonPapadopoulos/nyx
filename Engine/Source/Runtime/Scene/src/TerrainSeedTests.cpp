#include "NyxPCH.h"
#include "TerrainSeedTests.h"

#include "JobSystem.h"
#include "Profiler.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <map>

namespace Nyx
{
	namespace
	{
		double MillisecondsSince(std::chrono::steady_clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		}

		// Spots spread over the land when there are no places: plan nodes on a grid, those on land
		std::vector<glm::vec3> SpreadSpots(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan, uint32_t most)
		{
			std::vector<glm::vec3> spots;
			const int across = std::max(int(std::ceil(std::sqrt(float(most) * 2.0f))), 2);
			const int step = std::max(plan.Nodes / (across + 1), 1);
			for (int j = step; j < plan.Nodes && spots.size() < most; j += step)
			{
				for (int i = step; i < plan.Nodes && spots.size() < most; i += step)
				{
					if (plan.Landform[plan.IndexOf(i, j)] == TerrainWorldPlan::NotLand)
					{
						continue;
					}
					const glm::vec2 point = plan.PointOf(i, j);
					const TerrainWorldPlan::Bounds bounds = plan.GetRange(point - TerrainWorldPlan::CellSize, point + TerrainWorldPlan::CellSize);
					const float top = bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f + 4.0f;
					if (const std::optional<float> ground = FindTerrainGround(terrain, point, top, bounds.LowHeight - plan.Made.Thickness - 50.0f))
					{
						spots.push_back(glm::vec3(point.x, *ground, point.y));
					}
				}
			}
			return spots;
		}
	}

	TerrainSeedTest TestTerrainSeed(const SdfTerrainComponent& terrain, uint32_t seed, const TerrainSeedTestOptions& options, JobSystem* jobs)
	{
		NYX_PROFILE_FUNCTION();
		const auto start = std::chrono::steady_clock::now();
		TerrainSeedTest test;
		test.Seed = seed;
		test.Plan = MakeTerrainPlanSeed(seed, terrain.Plan);
		SdfTerrainComponent world = terrain;
		world.Seed = seed;
		world.Edits.clear();
		world.Planned = test.Plan.Plan;
		const bool bPlanned = UsesTerrainWorldPlan(world);

		// Its places and Route Checks
		if (options.bChecks && bPlanned && !world.PlaceRules.empty())
		{
			test.bChecked = true;
			const auto placesStart = std::chrono::steady_clock::now();
			test.Placement = std::make_shared<TerrainPlacement>(PlaceOnTerrain(world, jobs));
			test.PlacesMilliseconds = MillisecondsSince(placesStart);
			for (size_t index = 0; index < world.PlaceRules.size(); ++index)
			{
				const SdfTerrainPlaceRule& rule = world.PlaceRules[index];
				const TerrainPlacement::RuleResult& result = test.Placement->Rules[index];
				if (!rule.bEnabled || result.Found >= rule.Count)
				{
					continue;
				}
				size_t most = 0;
				for (size_t miss = 1; miss < result.Misses.size(); ++miss)
				{
					most = result.Misses[miss] > result.Misses[most] ? miss : most;
				}
				test.Failures.push_back(std::format("Place Rule {}: {} of {} found, {} from its Fallback; most tries missed by {}", rule.Name, result.Found, rule.Count,
					result.Fallbacks, GetTerrainPlaceMissName(ETerrainPlaceMiss(most))));
				test.FailureKeys.push_back(std::format("Place Rule {} ({})", rule.Name, GetTerrainPlaceMissName(ETerrainPlaceMiss(most))));
			}
			if (!world.RouteChecks.empty())
			{
				const auto waterStart = std::chrono::steady_clock::now();
				const TerrainWater water = MakeTerrainWater(world, nullptr, nullptr, nullptr, jobs);
				test.WaterMilliseconds = MillisecondsSince(waterStart);
				const auto traversalStart = std::chrono::steady_clock::now();
				const TerrainTraversal traversal = BuildTerrainTraversal(world, &water, jobs);
				test.Routes = CheckTerrainRoutes(world, *test.Placement, traversal);
				test.TraversalMilliseconds = MillisecondsSince(traversalStart);
				for (size_t index = 0; index < test.Routes.size(); ++index)
				{
					const SdfTerrainRouteCheck& check = world.RouteChecks[index];
					if (!test.Routes[index].bPassed)
					{
						test.Failures.push_back(std::format("Route Check {} to {}: {}", check.From, check.To, test.Routes[index].Why));
						test.FailureKeys.push_back(std::format("Route Check {} to {}", check.From, check.To));
					}
				}
			}
			test.bPassed = test.Failures.empty();
		}

		// The finest chunks at the places, or spread over the land, one at a time on this thread
		if (options.bChunks && bPlanned && options.MostChunks > 0)
		{
			std::vector<glm::vec3> spots;
			if (test.Placement)
			{
				for (const TerrainPlace& place : test.Placement->Places)
				{
					if (spots.size() < options.MostChunks)
					{
						spots.push_back(place.Position);
					}
				}
			}
			if (spots.empty())
			{
				spots = SpreadSpots(world, *test.Plan.Plan, options.MostChunks);
			}
			const int level = std::clamp(world.FinestLevel, TerrainFinestLevel, TerrainCoarsestLevel);
			const float size = GetTerrainChunkSize(level);
			for (const glm::vec3& spot : spots)
			{
				const TerrainChunkId id{ level, glm::ivec3(glm::floor(spot / size)) };
				const auto chunkStart = std::chrono::steady_clock::now();
				MakeTerrainChunkMesh(world, id);
				const double milliseconds = MillisecondsSince(chunkStart);
				if (milliseconds > test.WorstChunkMilliseconds)
				{
					test.WorstChunkMilliseconds = milliseconds;
					test.WorstChunkAt = spot;
				}
			}
		}
		test.Milliseconds = MillisecondsSince(start);
		return test;
	}

	TerrainSeedSummary SummariseTerrainSeeds(const std::vector<TerrainSeedTest>& tests)
	{
		TerrainSeedSummary summary;
		summary.Seeds = uint32_t(tests.size());
		if (tests.empty())
		{
			return summary;
		}
		std::vector<TerrainPlanSeed> plans;
		std::map<std::string, uint32_t> failing;
		for (const TerrainSeedTest& test : tests)
		{
			plans.push_back(test.Plan);
			summary.Checked += test.bChecked ? 1 : 0;
			summary.Passed += test.bChecked && test.bPassed ? 1 : 0;
			for (const std::string& key : test.FailureKeys)
			{
				++failing[key];
			}
		}
		summary.Spread = GetTerrainPlanSpread(plans);
		summary.FailingMost.assign(failing.begin(), failing.end());
		std::stable_sort(summary.FailingMost.begin(), summary.FailingMost.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		auto range = [&](const std::function<double(const TerrainSeedTest&)>& of)
		{
			TerrainPlanSpread::Range result{ float(of(tests.front())), 0.0f, float(of(tests.front())) };
			for (const TerrainSeedTest& test : tests)
			{
				const float value = float(of(test));
				result.Least = std::min(result.Least, value);
				result.Most = std::max(result.Most, value);
				result.Mean += value / float(tests.size());
			}
			return result;
		};
		summary.Water = range([](const TerrainSeedTest& test) { return test.WaterMilliseconds; });
		summary.Places = range([](const TerrainSeedTest& test) { return test.PlacesMilliseconds; });
		summary.Traversal = range([](const TerrainSeedTest& test) { return test.TraversalMilliseconds; });
		summary.WorstChunk = range([](const TerrainSeedTest& test) { return test.WorstChunkMilliseconds; });
		summary.Total = range([](const TerrainSeedTest& test) { return test.Milliseconds; });
		for (const TerrainSeedTest& test : tests)
		{
			if (float(test.WorstChunkMilliseconds) >= summary.WorstChunk.Most)
			{
				summary.WorstChunkSeed = test.Seed;
			}
		}
		return summary;
	}

	std::string WriteTerrainSeedReport(const SdfTerrainComponent& terrain, const std::vector<TerrainSeedTest>& tests, const TerrainSeedSummary& summary)
	{
		auto percent = [](const TerrainPlanSpread::Range& range) { return std::format("{:.0f}% ({:.0f} to {:.0f})", range.Mean * 100.0f, range.Least * 100.0f, range.Most * 100.0f); };
		auto count = [](const TerrainPlanSpread::Range& range) { return std::format("{:.1f} ({:.0f} to {:.0f})", range.Mean, range.Least, range.Most); };
		auto time = [](const TerrainPlanSpread::Range& range) { return std::format("{:.0f} ms ({:.0f} to {:.0f})", range.Mean, range.Least, range.Most); };
		std::string report = "# Seed tests\n\n";
		report += std::format("{} seeds of a {:.0f} m world, {} Place Rules, {} Route Checks.\n\n", summary.Seeds, terrain.Plan.Size, terrain.PlaceRules.size(),
			terrain.RouteChecks.size());
		if (summary.Checked > 0)
		{
			report += std::format("**{} of {} passed** ({:.0f}%).\n\n", summary.Passed, summary.Checked, 100.0 * double(summary.Passed) / double(summary.Checked));
		}
		else
		{
			report += "Not checked: the terrain has no Place Rules.\n\n";
		}
		if (!summary.FailingMost.empty())
		{
			report += "## Failing most\n\n| What | Seeds |\n|---|---|\n";
			for (const auto& [key, seeds] : summary.FailingMost)
			{
				report += std::format("| {} | {} |\n", key, seeds);
			}
			report += "\n";
		}
		report += "## Across the seeds\n\nMean (least to most).\n\n| | |\n|---|---|\n";
		report += std::format("| Sea | {} |\n| Rough land | {} |\n| Rifts | {} |\n| Tunnels | {} |\n", percent(summary.Spread.Sea), percent(summary.Spread.Roughland),
			percent(summary.Spread.Rift), percent(summary.Spread.Tunnels));
		report += std::format("| Rivers | {} |\n| Lakes | {} |\n", count(summary.Spread.Rivers), count(summary.Spread.Lakes));
		report += std::format("| Plan | {} |\n| Places | {} |\n| Water | {} |\n| Traversal and checks | {} |\n", time(summary.Spread.Milliseconds), time(summary.Places),
			time(summary.Water), time(summary.Traversal));
		report += std::format("| Worst chunk at the spots | {} (seed {}) |\n| A seed all told | {} |\n\n", time(summary.WorstChunk), summary.WorstChunkSeed, time(summary.Total));
		report += "## Seeds\n\n| Seed | Passed | Sea | Rough | Rifts | Tunnels | Rivers | Lakes | Worst chunk | Failures |\n|---|---|---|---|---|---|---|---|---|---|\n";
		for (const TerrainSeedTest& test : tests)
		{
			std::string failures;
			for (const std::string& failure : test.Failures)
			{
				failures += (failures.empty() ? "" : "; ") + failure;
			}
			const TerrainPlanShares& shares = test.Plan.Shares;
			report += std::format("| {} | {} | {:.0f}% | {:.0f}% | {:.0f}% | {:.0f}% | {} | {} | {:.1f} ms | {} |\n", test.Seed, !test.bChecked ? "-" : (test.bPassed ? "yes" : "no"),
				shares.Sea * 100.0f, shares.Roughland * 100.0f, shares.Rift * 100.0f, shares.Tunnels * 100.0f, shares.Rivers, shares.Lakes, test.WorstChunkMilliseconds, failures);
		}
		return report;
	}
}

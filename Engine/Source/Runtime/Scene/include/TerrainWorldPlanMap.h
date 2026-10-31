#pragma once

#include "SdfTerrainComponent.h"
#include "TerrainWorldPlan.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	// What a world plan's map shows on its kinds of ground, a pixel a node (16 m)
	enum class ETerrainPlanMapOverlay : uint8_t
	{
		// The kinds alone: plains green, rough land brown, rifts black, sea blue
		Kinds,
		// Darker where the plan's ground is lower, from its lowest to its highest
		Height,
		// Lighter where the plan's ground is steeper, 0 to 45 degrees
		Slope,
		// Violet where tunnels run under it, by how open they are
		Tunnels
	};

	// A world plan drawn from above: a pixel a node, row 0 at the plan's Origin (its least Z), the
	// kinds in their colours, shaded or coloured by the overlay; off the landmass clear; rivers and
	// lakes light blue over it all. 8 bits a channel, straight alpha, sRGB-encoded as the editor's
	// images are.
	struct TerrainPlanMapImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;
	};
	TerrainPlanMapImage DrawTerrainWorldPlanMap(const TerrainWorldPlan& plan, ETerrainPlanMapOverlay overlay = ETerrainPlanMapOverlay::Kinds);

	// What the plan made a place to be, at a point along X and Z in the terrain's space
	struct TerrainPlanReadout
	{
		bool bLand = false;
		ESdfTerrainLandform Landform = ESdfTerrainLandform::Plains;
		// The plan's ground there, before the plains' noise and rough land's crests; its slope in
		// degrees; how open tunnels under it are, 0 to 1; how far the nearest rift's floor is, in
		// metres (up to TerrainWorldPlan::MaxReach)
		float Height = 0.0f;
		float SlopeDegrees = 0.0f;
		float Tunnels = 0.0f;
		float RiftDistance = 0.0f;
	};
	TerrainPlanReadout ReadTerrainWorldPlan(const TerrainWorldPlan& plan, const glm::vec2& point);

	// How much of a plan's landmass each kind is, 0 to 1, by its nodes (tunnels where they are more
	// than half open), as the Gate 3 report counts them; and how many rivers and lakes
	struct TerrainPlanShares
	{
		float Sea = 0.0f;
		float Roughland = 0.0f;
		float Rift = 0.0f;
		float Tunnels = 0.0f;
		size_t Rivers = 0;
		size_t Lakes = 0;
	};
	TerrainPlanShares GetTerrainWorldPlanShares(const TerrainWorldPlan& plan);

	// One seed of a plan, for comparing many (the editor's Plan Seeds): its plan, shares and how long
	// it took to make. Safe on any thread.
	struct TerrainPlanSeed
	{
		uint32_t Seed = 0;
		std::shared_ptr<const TerrainWorldPlan> Plan;
		TerrainPlanShares Shares;
		double Milliseconds = 0.0;
	};
	TerrainPlanSeed MakeTerrainPlanSeed(uint32_t seed, const SdfTerrainWorldPlan& plan);

	// How the seeds' shares spread: each kind's least, mean and most, and the rivers' and lakes'
	// fewest and most
	struct TerrainPlanSpread
	{
		struct Range
		{
			float Least = 0.0f;
			float Mean = 0.0f;
			float Most = 0.0f;
		};
		Range Sea;
		Range Roughland;
		Range Rift;
		Range Tunnels;
		Range Rivers;
		Range Lakes;
		Range Milliseconds;
	};
	TerrainPlanSpread GetTerrainPlanSpread(const std::vector<TerrainPlanSeed>& seeds);
}

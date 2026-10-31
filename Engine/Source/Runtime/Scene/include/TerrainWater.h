#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "JobSystem.h"
#include "MeshData.h"
#include "RuntimeMeshComponent.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	class Registry;

	// Where an SDF terrain's water stands, as its world plan put it and as the ground is now, edits and
	// all: a state, simplified on purpose, never simulated. Worked out from the terrain alone, so the
	// same ground gives the same water wherever and whenever it is worked out, after a load too.
	//
	// - The sea: every place connected to the plan's sea whose ground is below the sea's level, filled
	//   to it; where it reaches a place with no ground (past the edge, down a rift cut through), it
	//   pours over in a fall.
	// - Lakes: from each of the plan's lakes' middles, filled up to where they spill; a lake whose rim
	//   is cut drops to the cut.
	// - Rivers: along their planned beds, their water a little below the ground beside them; where the
	//   ground under a bed is gone (a rift or a dig across it) the river falls to the ground below, or
	//   off into the air, and from there runs on downhill, pooling where it can't, to the sea, a lake
	//   or over an edge. Its course below the cut is dry.
	struct TerrainWater
	{
		static constexpr float CellSize = 8.0f;
		// Cells per side, the first's middle at Origin, x fastest
		int Cells = 0;
		glm::vec2 Origin{ 0.0f };

		// Each cell's ground: the first surface down from above at its middle; NoGround where there is
		// none
		static constexpr float NoGround = -1.0e30f;
		std::vector<float> Ground;

		enum class EBody : uint8_t
		{
			None,
			Sea,
			Lake
		};
		// What stands on each cell, and its water's height
		std::vector<EBody> Body;
		std::vector<float> Level;

		struct River
		{
			// Its water's surface along it, from its source
			std::vector<glm::vec3> Points;
			float Width = 0.0f;
		};
		std::vector<River> Rivers;

		// A sheet of falling water: from a line at Top, Across either way, down to Bottom
		struct Fall
		{
			glm::vec3 Top{ 0.0f };
			glm::vec3 Across{ 1.0f, 0.0f, 0.0f };
			float Bottom = 0.0f;
		};
		std::vector<Fall> Falls;

		size_t IndexOf(int i, int j) const { return size_t(i) + size_t(j) * size_t(Cells); }
		glm::vec2 PointOf(int i, int j) const { return Origin + glm::vec2(float(i), float(j)) * CellSize; }
		// The cell a point is in, clamped to the grid
		glm::ivec2 CellOf(const glm::vec2& point) const;
	};

	// Works the terrain's water out (it must have a Plan layer). With `earlier`, worked out from the
	// same layers, its ground is kept outside the box from `changedMin` to `changedMax` (the edits since)
	// and found again only inside it. The ground's rays on `jobs`' workers, if given.
	TerrainWater MakeTerrainWater(const SdfTerrainComponent& terrain, const TerrainWater* earlier = nullptr, const glm::vec3* changedMin = nullptr,
		const glm::vec3* changedMax = nullptr, JobSystem* jobs = nullptr);

	// The water drawn in flat colours, in the terrain's space: the sea and lakes as flat surfaces over
	// their cells and those beside them (the ground hides what is under it), rivers as ribbons along
	// their water, falls as sheets seen from both sides
	MeshData MakeTerrainWaterMesh(const TerrainWater& water);

	namespace Detail
	{
		struct TerrainWaterWork;
	}

	// An SDF terrain's water while the world runs, in the editor and the game: worked out on a worker
	// whenever its layers, plan or edits change, the ground found again only where edits reached, and
	// drawn with the terrain (its Mesh Renderer's material, the water's vertex colours). Run-time only,
	// never saved.
	struct TerrainWaterComponent
	{
		std::shared_ptr<const TerrainWater> Water;
		RuntimeMeshComponent Mesh;
		// How many times it was worked out, and how long the last took
		uint32_t Made = 0;
		double LastMilliseconds = 0.0;
		std::shared_ptr<Detail::TerrainWaterWork> Work;
	};

	// Keeps every SDF terrain's water with a Plan layer up to date; called by UpdateTerrainChunks()
	void UpdateTerrainWater(Registry& world, JobSystem& jobs);

	std::vector<InspectedValue> DescribeTerrainWater(const TerrainWaterComponent& component);
}

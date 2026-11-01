#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "JobSystem.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class Registry;
	struct TerrainPlacement;
	struct TerrainWater;

	// What crossing from one floor to the next needs, a bit each
	enum ETerrainTraversalTag : uint8_t
	{
		// Along the ground no steeper than the walking slope, or down no farther than a safe drop
		TerrainWalk = 1 << 0,
		// Up more than walking can, up to the climbing height
		TerrainClimb = 1 << 1,
		// Over a gap no wider than the jumping width, to ground about as high
		TerrainGap = 1 << 2,
		// Through water: either end under the sea's or a lake's level
		TerrainSwim = 1 << 3,
		TerrainAllTags = TerrainWalk | TerrainClimb | TerrainGap | TerrainSwim
	};

	// How an SDF terrain's world can be crossed, from queries of its field (BuildTerrainTraversal()):
	// columns Spacing apart over the plan's square, each with its floors from above (ground with
	// room for a character over it: the top, a rift's floor, a tunnel's, an island's), and from each
	// floor directed edges to the floors of the columns round it, tagged by what crossing needs.
	// Built from the terrain's layers and plan, never its edits, as a world is checked when it is
	// made.
	struct TerrainTraversal
	{
		SdfTerrainTraversal Values;
		// Columns per side, the first's middle at Origin, x fastest
		int Columns = 0;
		glm::vec2 Origin{ 0.0f };

		// Each column's floors are FloorHeights[ColumnFirst[c]] up to ColumnFirst[c + 1], highest
		// first; a floor is a node of the graph by its index there
		std::vector<uint32_t> ColumnFirst;
		std::vector<float> FloorHeights;
		// Whether a floor is under water, and the water's level over it
		std::vector<uint8_t> Wet;

		// A floor's edges are EdgeTargets[EdgeFirst[n]] up to EdgeFirst[n + 1], with their tags
		std::vector<uint32_t> EdgeFirst;
		std::vector<uint32_t> EdgeTargets;
		std::vector<uint8_t> EdgeTags;

		double Milliseconds = 0.0;

		size_t Floors() const { return FloorHeights.size(); }
		size_t IndexOf(int i, int j) const { return size_t(i) + size_t(j) * size_t(Columns); }
		glm::vec2 PointOf(int i, int j) const { return Origin + glm::vec2(float(i), float(j)) * Values.Spacing; }
		// The column of a floor, and the floor's place
		uint32_t ColumnOf(uint32_t floor) const;
		glm::vec3 PlaceOf(uint32_t floor) const;
	};

	// Builds the terrain's traversal graph (it must have a Plan layer, else an empty one), the water
	// from `water` if given (swimming nowhere otherwise); the columns on `jobs`' workers, if given.
	// The same graph either way.
	TerrainTraversal BuildTerrainTraversal(const SdfTerrainComponent& terrain, const TerrainWater* water = nullptr, JobSystem* jobs = nullptr);

	// The floor nearest a point: in its column, or the columns round it, the floor nearest its
	// height within `reach` metres along X and Z; nothing if there is none
	std::optional<uint32_t> FindTerrainFloor(const TerrainTraversal& traversal, const glm::vec3& point, float reach = 8.0f);

	// The cheapest way from one floor to another by edges with only the tags allowed, each edge
	// costing its length, climbs and gaps twice that, swims three times: its floors from the first to
	// the last and the tags it took; nothing if there is none
	struct TerrainRoute
	{
		std::vector<uint32_t> Floors;
		uint8_t Tags = 0;
		float Length = 0.0f;
	};
	std::optional<TerrainRoute> FindTerrainRoute(const TerrainTraversal& traversal, uint32_t from, uint32_t to, uint8_t tags);

	// What each floor needs to be reached from `from`: 0 for none, else the least of these, each
	// allowing the tags before it too: Walk, Walk and Swim, those and Climb, all
	std::vector<uint8_t> GetTerrainReach(const TerrainTraversal& traversal, uint32_t from);

	// How many separate ways lead from `from` to a place (within `radius` of `to` along X and Z),
	// by edges with only the tags allowed, counted up to `most`: ways sharing no cell of 16 m on the
	// way (vertex-disjoint over cells), so a tunnel or a ramp counts once however wide it is
	uint32_t CountTerrainApproaches(const TerrainTraversal& traversal, uint32_t from, const glm::vec3& to, float radius, uint8_t tags, uint32_t most = 8);

	// The cells the approaches are counted over, for tags, made once for many places: each floor's
	// node (a 16 m cell's floors joined within it), and the nodes split in two, in and out, with the
	// arcs between them
	struct TerrainApproachCells
	{
		uint8_t Tags = 0;
		std::vector<uint32_t> NodeOf;
		uint32_t Nodes = 0;
		std::vector<uint32_t> ArcTo;
		std::vector<int32_t> ArcCapacity;
		std::vector<uint32_t> VertexFirst;
		std::vector<uint32_t> VertexArcs;
	};
	TerrainApproachCells MakeTerrainApproachCells(const TerrainTraversal& traversal, uint8_t tags);
	uint32_t CountTerrainApproaches(const TerrainApproachCells& cells, const TerrainTraversal& traversal, uint32_t from, const glm::vec3& to, float radius, uint32_t most = 8);

	// Which floors can be reached from `from` by edges with only the tags allowed, 1 each
	std::vector<uint8_t> ReachTerrainFloors(const TerrainTraversal& traversal, uint32_t from, uint8_t tags);

	// A Route Check (SdfTerrainRouteCheck) as the world is: whether every place of its To rule can be
	// reached from the first place of its From rule, with how many separate ways each
	struct TerrainRouteResult
	{
		bool bPassed = false;
		// Places of To reached, of how many; the fewest approaches any had
		uint32_t Reached = 0;
		uint32_t Places = 0;
		uint32_t FewestApproaches = 0;
		std::string Why;
	};
	std::vector<TerrainRouteResult> CheckTerrainRoutes(const SdfTerrainComponent& terrain, const TerrainPlacement& placement, const TerrainTraversal& traversal);

	namespace Detail
	{
		struct TerrainTraversalWork;
	}

	// An SDF terrain's traversal graph and its Route Checks while the world runs, in the editor and the
	// game: built on a worker, after its water and places, whenever its layers, plan, seed, rules or
	// traversal values change. Run-time only, never saved.
	struct TerrainTraversalComponent
	{
		std::shared_ptr<const TerrainTraversal> Traversal;
		std::vector<TerrainRouteResult> Routes;
		uint64_t Key = 0;
		uint32_t Made = 0;
		std::shared_ptr<Detail::TerrainTraversalWork> Work;
	};

	// Keeps every SDF terrain's traversal graph with a Plan layer, Place Rules and Route Checks up to
	// date; called by UpdateTerrainChunks()
	void UpdateTerrainTraversal(Registry& world, JobSystem& jobs);

	std::vector<InspectedValue> DescribeTerrainTraversal(const TerrainTraversalComponent& component);
}

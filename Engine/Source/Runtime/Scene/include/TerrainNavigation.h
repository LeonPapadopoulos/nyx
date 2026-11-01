#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "JobSystem.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Nyx
{
	class DebugDraw;
	class Registry;
	struct TerrainWater;

	namespace Detail
	{
		struct TerrainNavigationState;
	}

	// The navigation tiles are 32 m squares along X and Z, each a column of the finest chunks
	inline constexpr float TerrainNavigationTileSize = 32.0f;

	// A navigation tile's polygons are walkable on the ground, or under the water's level
	enum ETerrainNavigationArea : uint8_t
	{
		TerrainNavigationWalk = 1,
		TerrainNavigationSwim = 2
	};

	// One tile made on a worker, in Detour's form, ready to go in: its column, and what it was made from
	struct TerrainNavigationTile
	{
		glm::ivec2 Column{ 0 };
		std::vector<uint8_t> Data;
		uint32_t Polygons = 0;
		// All of it, and of that the field meshed, on its worker
		double Milliseconds = 0.0;
		double SampleMilliseconds = 0.0;
		uint32_t Slabs = 0;
		uint32_t Triangles = 0;
	};

	// Makes the navigation tile of a column (TerrainNavigationTileSize along X and Z from Column times
	// it) from the terrain's field and the edits given: the field meshed at 1 m cells, as the finest
	// chunks and collision are, in slabs where the surface can be (by the field's value ranges), with a
	// border round the tile so its edges meet its neighbours'; Recast's walkable polygons for a
	// character of the terrain's Navigation values, those under the water's level (if given) to swim.
	// Empty Data where nothing is walkable. Safe on any thread.
	TerrainNavigationTile MakeTerrainNavigationTile(const SdfTerrainComponent& terrain, std::span<const SdfTerrainEdit> edits, const glm::ivec2& column,
		const TerrainWater* water = nullptr);

	// An SDF terrain's navigation while the world runs (SdfTerrainComponent::Navigation): the tiles
	// round the viewpoints in one Detour navigation mesh, made on the workers and put in as they come,
	// made again where edits reach. Run-time only, never saved.
	struct TerrainNavigationComponent
	{
		std::shared_ptr<Detail::TerrainNavigationState> State;
		// The tiles in, those being made, and their polygons
		uint32_t Tiles = 0;
		uint32_t Pending = 0;
		uint32_t Polygons = 0;
		// Tiles made since it was added, and the last one's time on its worker
		uint64_t Made = 0;
		double LastMilliseconds = 0.0;
	};

	// Keeps every SDF terrain's navigation with Navigation on up to date round the viewpoints (in the
	// terrain's space); called by UpdateTerrainChunks()
	void UpdateTerrainNavigation(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs);

	// Whether the tiles round every viewpoint are in and none is being made
	bool IsTerrainNavigationSettled(const TerrainNavigationComponent& component);

	// The way along the navigation polygons from the polygon nearest `from` to the one nearest `to`
	// (within 4 m across and 8 m up or down), its corners from `from` to `to`; through the water too
	// if asked. Nothing where either has no polygon near it or no way joins them. In the terrain's
	// space.
	std::optional<std::vector<glm::vec3>> FindTerrainNavigationPath(const TerrainNavigationComponent& component, const glm::vec3& from, const glm::vec3& to,
		bool bSwim = false);

	// The nearest point on the navigation polygons to a point, within 4 m across and 8 m up or down
	std::optional<glm::vec3> FindTerrainNavigationPoint(const TerrainNavigationComponent& component, const glm::vec3& point);

	// The tiles' polygons round a point as lines, walkable green, under water blue, moved into the
	// world by `toWorld`
	void DrawTerrainNavigation(DebugDraw& drawing, const TerrainNavigationComponent& component, const glm::mat4& toWorld, const glm::vec3& around, float radius);

	std::vector<InspectedValue> DescribeTerrainNavigation(const TerrainNavigationComponent& component);
}

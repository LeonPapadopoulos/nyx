#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "EntityGuid.h"
#include "JobSystem.h"
#include "ReflectionMacros.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	class Registry;

	// One place a Place Rule found (SdfTerrainPlaceRule): its middle on the ground, in the terrain's
	// space, turned about its up by Yaw (radians, from its seed); a Fallback place where the rule
	// couldn't find enough
	struct TerrainPlace
	{
		uint32_t Rule = 0;
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		bool bFallback = false;
	};

	// The places an SDF terrain's Place Rules found when its world was made: rule by rule, in their
	// order, each rule's places in the order they were found. Found on the terrain's layers and plan
	// alone, never its edits, so the same world gives the same places, after a load too.
	struct TerrainPlacement
	{
		std::vector<TerrainPlace> Places;

		// How each rule did (a PlaceRules index each): the places it found itself and those its
		// Fallback gave, how many seeds it took (1: its first) and how many places it tried
		struct RuleResult
		{
			uint32_t Found = 0;
			uint32_t Fallbacks = 0;
			uint32_t Rolls = 0;
			uint32_t Tried = 0;
			// Why most of its tries didn't fit, a count each (ETerrainPlaceMiss)
			std::vector<uint32_t> Misses;
		};
		std::vector<RuleResult> Rules;

		double Milliseconds = 0.0;
	};

	// Why a place tried didn't fit, in the order a place is tried
	enum class ETerrainPlaceMiss : uint8_t
	{
		// Off the landmass, or on a kind of ground the rule doesn't allow
		Ground,
		// No ground under it, or outside the Height Range
		Height,
		// Under the sea's or a lake's level
		Water,
		// The ground round it too steep, or gone
		Slope,
		// Terrain over it within the Clearance
		Clearance,
		// Too near another place, or not near enough to the Near rule's
		Distance,
		Count
	};
	const char* GetTerrainPlaceMissName(ETerrainPlaceMiss miss);

	// Finds the places of the terrain's Place Rules (it must have a Plan layer, else none): for each
	// rule, places tried at random on the landmass from its seed (the terrain's and its Name's), each
	// checked by the plan's kinds of ground and water and by the field (its ground from above, the
	// ground round its footprint, the clearance over it), then kept if apart enough and near enough;
	// with the next seed where not enough were found, up to 8, then its Fallback places. The tries'
	// checks on `jobs`' workers, if given; the same places either way.
	TerrainPlacement PlaceOnTerrain(const SdfTerrainComponent& terrain, JobSystem* jobs = nullptr);

	// Whether every enabled rule found all its places itself
	bool IsTerrainPlacementMet(const SdfTerrainComponent& terrain, const TerrainPlacement& placement);

	// The first enabled rule of that Name; nothing if there is none
	std::optional<uint32_t> FindTerrainPlaceRule(const SdfTerrainComponent& terrain, std::string_view name);

	// A hash of the rules, for what places are found from
	uint64_t HashTerrainPlaceRules(const std::vector<SdfTerrainPlaceRule>& rules);

	namespace Detail
	{
		struct TerrainPlacesWork;
	}

	// An SDF terrain's places while the world runs, in the editor and the game: found on a worker
	// whenever its layers, plan, seed or Place Rules change, never its edits. Run-time only, never
	// saved.
	struct TerrainPlacesComponent
	{
		std::shared_ptr<const TerrainPlacement> Placement;
		// What they were found from (layers, plan, seed and rules)
		uint64_t Key = 0;
		uint32_t Made = 0;
		std::shared_ptr<Detail::TerrainPlacesWork> Work;
	};

	// Keeps every SDF terrain's places with a Plan layer and Place Rules up to date, then the Terrain
	// Place Copies; called by UpdateTerrainChunks()
	void UpdateTerrainPlaces(Registry& world, JobSystem& jobs);

	std::vector<InspectedValue> DescribeTerrainPlaces(const TerrainPlacesComponent& component);

	// Copies of its entity's mesh at the places one of an SDF terrain's Place Rules found, turned by
	// each place's Yaw: placeholders for what a world puts there. Made while the world runs, in the
	// entity's MeshInstancesComponent, never saved, again whenever the places or where the entities
	// are change.
	NYX_REFLECT(Component, meta = (DisplayName = "Terrain Place Copies"))
	struct TerrainPlaceCopiesComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The entity with the SDF Terrain whose places they are"))
		EntityGuid Terrain;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The Name of the terrain's Place Rule"))
		std::string Rule = "Place";

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "Each copy's size"))
		float Scale = 1.0f;

		// What the copies were made from
		uint64_t MadeFrom = 0;
	};

	// Makes every Terrain Place Copies' instances again where what they are made from changed
	void UpdateTerrainPlaceCopies(Registry& world);

	void RemoveTerrainPlaceCopies(Registry& world, Entity entity);
}

#include "Generated/Runtime/TerrainPlacement.reflect.h"

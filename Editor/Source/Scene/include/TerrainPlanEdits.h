#pragma once

#include "Entity.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Nyx::Editor
{
	class SceneEditor;

	// The terrain whose world plan the Plan Map and Plan Seeds show: the selected entity if it is an SDF
	// terrain with a Plan layer, else the scene's first
	std::optional<Nyx::Entity> FindPlannedTerrain(SceneEditor& editing);

	// Edits of an SDF terrain's world plan from its map (TerrainPlanWindow), each one undo step that
	// running games get. Return false, and change nothing, for an entity without a terrain or when
	// nothing would change.

	// The plan's pins become `pins` (moved, resized, added, changed or removed). A pin removed takes
	// none of its hand overrides with it: they stay where they are, the terrain's; those of the pins
	// after it are numbered down with them.
	bool SetTerrainPlanPins(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanPin>& pins, const char* label);

	// The plan's hand overrides become `overrides` (drawn, changed or removed)
	bool SetTerrainPlanOverrides(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanOverride>& overrides, const char* label);

	// A hand override of points in the world given to the pin it starts in (its first point within
	// a pin's ring, the last drawn first, as it lies on top), its points made from the pin's middle;
	// else the terrain's
	Nyx::SdfTerrainPlanOverride AnchorTerrainPlanOverride(const std::vector<Nyx::SdfTerrainPlanPin>& pins, Nyx::SdfTerrainPlanOverride override);

	// The hand override under a point of the plan, `tolerance` metres either way of its stroke or
	// stamps; the last drawn first
	std::optional<size_t> FindTerrainPlanOverrideAt(const Nyx::SdfTerrainWorldPlan& plan, const glm::vec2& point, float tolerance);

	// The terrain's seed: another world of the same plan
	bool SetTerrainSeed(SceneEditor& editing, Nyx::Entity terrain, uint32_t seed);

	// The pin under a point of the plan (along X and Z, in the terrain's space), `tolerance` metres
	// either way: a place of its path, else its middle, else its ring; the last drawn first, as it
	// lies on top
	struct TerrainPinHit
	{
		size_t Index = 0;
		// Its ring, to size it, rather than its middle, to move it
		bool bRing = false;
		// A place of its path (SdfTerrainPlanPin::Path), to move; -1 for none
		int Place = -1;
	};
	std::optional<TerrainPinHit> FindTerrainPinAt(const std::vector<Nyx::SdfTerrainPlanPin>& pins, const glm::vec2& point, float tolerance);
}

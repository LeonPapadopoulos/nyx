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

	// Edits of an SDF terrain's world plan from its map (TerrainPlanWindow), each one undo step that
	// running games get. Return false, and change nothing, for an entity without a terrain or when
	// nothing would change.

	// The plan's pins become `pins` (moved, resized, added, changed or removed)
	bool SetTerrainPlanPins(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanPin>& pins, const char* label);

	// The terrain's seed: another world of the same plan
	bool SetTerrainSeed(SceneEditor& editing, Nyx::Entity terrain, uint32_t seed);

	// The pin under a point of the plan (along X and Z, in the terrain's space), `tolerance` metres
	// either way: its middle, else its ring; the last drawn first, as it lies on top
	struct TerrainPinHit
	{
		size_t Index = 0;
		// Its ring, to size it, rather than its middle, to move it
		bool bRing = false;
	};
	std::optional<TerrainPinHit> FindTerrainPinAt(const std::vector<Nyx::SdfTerrainPlanPin>& pins, const glm::vec2& point, float tolerance);
}

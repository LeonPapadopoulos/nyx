#pragma once

#include "Entity.h"
#include "SdfTerrain.h"

namespace Nyx
{
	class CommandQueue;
	struct EntityGuidLookup;

	// Which of a terrain's bricks are due a bake, on its entity: kept by ApplyTerrainEditCommands().
	// Run-time only, never saved.
	struct TerrainBakeCountsComponent
	{
		TerrainBakeCounts Counts;
	};

	// The step's SdfTerrainEditCommands, onto their terrains' edits in the order they came, each with
	// the terrain's next revision (a command's edit is a shape: only the host bakes), then the bricks
	// they made due baked (BakeTerrainBricks()). GameWorld::Step() calls it before its systems; the
	// terrain's collision takes the edits the next step.
	void ApplyTerrainEditCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands);
}

#pragma once

#include "Entity.h"

namespace Nyx
{
	class CommandQueue;
	struct EntityGuidLookup;

	// The step's SdfTerrainEditCommands, onto their terrains' edits in the order they came, each with
	// the terrain's next revision. GameWorld::Step() calls it before its systems; the terrain's
	// collision takes the edits the next step.
	void ApplyTerrainEditCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands);
}

#pragma once

#include "Entity.h"

namespace Nyx
{
	class CommandQueue;
	struct EntityGuidLookup;

	// The step's DestroyEntityCommands, DetachEntityCommands and AttachEntityCommands (EntityCommands.h),
	// in that order, each kind in the order they came; those that can't be applied are dropped with a
	// warning. `guids` must be up to date with the world, the step's spawns too. GameWorld::Step()
	// calls it after the spawns, before its systems. Attached followers are placed from this step's
	// end on; the bodies of what was destroyed go at the next step's start, as a spawned instance's
	// are made then.
	void ApplyHierarchyCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands);
}

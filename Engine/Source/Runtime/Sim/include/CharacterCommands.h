#pragma once

#include "Entity.h"

namespace Nyx
{
	class CommandQueue;
	struct EntityGuidLookup;

	// The step's MoveCharacterCommands, into their characters' CharacterComponents, in the order they
	// came: the last move for each character becomes its DesiredMove, and any jump asks it to jump.
	// A command whose guid names no character is dropped. GameWorld::Step() calls it before its
	// systems, so they see where the players want to go and can still change it.
	void ApplyMoveCharacterCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands);
}

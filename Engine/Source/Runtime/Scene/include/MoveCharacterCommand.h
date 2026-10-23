#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// A player's ask to walk a character (CharacterComponent), as a command (CommandQueue): the
	// game world applies it at the start of the next step, before its systems
	// (ApplyMoveCharacterCommands()). Of several in one step for the same character, the last move
	// counts and any jump does. The move stays until the next command, so a frame that runs two
	// steps walks in both.
	NYX_REFLECT()
	struct MoveCharacterCommand
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Character;

		// Where to walk, in world space along the ground, up to 1 long (the character's walk speed),
		// as CharacterComponent::DesiredMove
		NYX_PROPERTY(Serialize)
		glm::vec3 Move{ 0.0f };

		NYX_PROPERTY(Serialize)
		bool bJump = false;
	};
}

#include "Generated/Runtime/MoveCharacterCommand.reflect.h"

#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

namespace Nyx
{
	// A player's commands (CommandQueue) that take entities away and change what they follow; the
	// game world applies them at the start of the next step, after the step's spawns, before its
	// systems (ApplyHierarchyCommands()): every DestroyEntityCommand, then every DetachEntityCommand,
	// then every AttachEntityCommand, each kind in the order they came. They name entities by guid, so
	// they can name an instance spawned earlier in the same step.

	// Takes the entity away with everything below it (its children, at any depth), as deleting it in
	// the editor does: an instance of a prefab goes with its parts. A guid the world hasn't got is
	// dropped.
	NYX_REFLECT()
	struct DestroyEntityCommand
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Entity;
	};

	// Gives the entity a parent, keeping it where it is in the world: its local values are worked out
	// from where both are (SetLocalTransform()). Following, it comes along when the parent moves
	// (ParentComponent::bFollowInGame, TransformFollowers), e.g. a thing picked up; a dynamic body
	// doesn't follow until its game makes it kinematic. Dropped: a guid the world hasn't got, either
	// without a transform, or a parent that is the entity or below it.
	NYX_REFLECT()
	struct AttachEntityCommand
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Entity;

		NYX_PROPERTY(Serialize)
		EntityGuid Parent;

		NYX_PROPERTY(Serialize)
		bool bFollow = true;
	};

	// Takes the entity's parent away, leaving it where it is in the world, e.g. a thing dropped. An
	// entity without a parent, or a guid the world hasn't got, is dropped.
	NYX_REFLECT()
	struct DetachEntityCommand
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Entity;
	};
}

#include "Generated/Runtime/EntityCommands.reflect.h"

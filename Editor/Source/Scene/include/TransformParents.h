#pragma once

#include "Entity.h"

#include <optional>

namespace Nyx::Editor
{
	class SceneDocument;
	class TransactionSystem;

	// The entity's parent (ParentComponent), if it has one the world has
	std::optional<Nyx::Entity> FindParent(const Nyx::Registry& world, Nyx::Entity entity);

	// Whether `entity` is `ancestor` itself, or below it however deep
	bool IsSelfOrBelow(const Nyx::Registry& world, Nyx::Entity entity, Nyx::Entity ancestor);

	// Gives the entity another parent, or none, keeping it where it is in the world, as one undo step:
	// its local values are worked out from where it and the parent are. Both need a transform. Returns
	// false, and changes nothing, if the parent is the entity or below it, if either has no transform,
	// or if the entity has that parent already.
	bool SetParent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity, std::optional<Nyx::Entity> parent);

	// After the entity was moved in the world by hand, as the gizmo does: its local values follow,
	// so it stays where it was put. Nothing for an entity without a parent.
	void FollowWorldTransform(Nyx::Registry& world, Nyx::Entity entity);

	// Whether an entity above this one moves while a game runs (a rigid body or a character): a game
	// has no hierarchy, so the entity stays behind there
	bool HasParentMovingInGame(const Nyx::Registry& world, Nyx::Entity entity);
}

#pragma once

#include "Entity.h"

#include <string>

namespace Nyx
{
	struct ComponentTypeOps;
}

namespace Nyx::Editor
{
	class SceneDocument;
	class TransactionSystem;

	// Whether the details panel offers to add and remove components of this type. Not the guid
	// (undo, files and the game link find entities by it) or the name (the outliner shows it):
	// every entity keeps exactly those. Not a run-time type, which the world makes itself.
	bool CanAddOrRemoveComponent(const Nyx::ComponentTypeOps& componentType);

	// Adds a component with its default values to the entity, and records that for undo. Returns
	// false, and changes nothing, if the entity has one already or the type can't be added.
	bool AddComponent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity,
		const Nyx::ComponentTypeOps& componentType);

	// Removes the entity's component of this type, and records it, whole, for undo. Returns false,
	// and changes nothing, if the entity has none or the type can't be removed.
	bool RemoveComponent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity,
		const Nyx::ComponentTypeOps& componentType);

	// Gives the entity's MeshRenderer the material at `path` ("Materials/Stone.nyxmat"), and
	// records that for undo; running games get it as any edit. Returns false, and changes
	// nothing, if the entity has no MeshRenderer or that material already.
	bool AssignMaterial(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity, const std::string& path);
}

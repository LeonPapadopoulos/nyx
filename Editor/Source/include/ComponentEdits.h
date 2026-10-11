#pragma once

#include "Entity.h"

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Engine
{
	struct ComponentTypeOps;
}

namespace Nyx::Editor
{
	class TransactionSystem;

	// Whether the details panel offers to add and remove components of this type. Not the guid
	// (undo, files and the game link find entities by it) or the name (the outliner shows it):
	// every entity keeps exactly those.
	bool CanAddOrRemoveComponent(const Nyx::Engine::ComponentTypeOps& componentType);

	// Adds a component with its default values to the entity, and records that for undo. Returns
	// false, and changes nothing, if the entity has one already or the type can't be added.
	bool AddComponent(Nyx::SceneDocument& scene, TransactionSystem& transactions, Nyx::Engine::Entity entity,
		const Nyx::Engine::ComponentTypeOps& componentType);

	// Removes the entity's component of this type, and records it, whole, for undo. Returns false,
	// and changes nothing, if the entity has none or the type can't be removed.
	bool RemoveComponent(Nyx::SceneDocument& scene, TransactionSystem& transactions, Nyx::Engine::Entity entity,
		const Nyx::Engine::ComponentTypeOps& componentType);
}

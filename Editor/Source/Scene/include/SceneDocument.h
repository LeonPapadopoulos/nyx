#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "NameComponent.h"
#include "SceneOutline.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace Nyx
{
	class Registry;
}

namespace Nyx::Editor
{
	class SceneDocument
	{
	public:
		SceneDocument() = default;

		Nyx::Registry& GetRegistry() { return Registry; }
		const Nyx::Registry& GetRegistry() const { return Registry; }

		Nyx::Entity CreateEntity(const std::string& name = "Entity");
		bool DestroyEntity(Nyx::Entity entity);

		// The live entity with this guid. Undo steps name entities by guid, since a handle's slot is
		// reused once its entity is deleted, e.g. by the next entity added.
		std::optional<Nyx::Entity> FindEntity(Nyx::EntityGuid guid) const;

		// The entity's guid; invalid if it has none
		Nyx::EntityGuid GetGuid(Nyx::Entity entity) const;

		std::optional<Nyx::Entity>& GetSelection() { return SelectedEntity; }
		const std::optional<Nyx::Entity>& GetSelection() const { return SelectedEntity; }

		// The outliner's folders and order, saved with the scene (SceneOutlineEdits.h). It may list
		// entities that are gone, e.g. deleted ones, so that undo brings them back to their place.
		Nyx::SceneOutline& GetOutline() { return Outline; }
		const Nyx::SceneOutline& GetOutline() const { return Outline; }

	private:
		Nyx::Registry Registry;
		Nyx::SceneOutline Outline;
		std::optional<Nyx::Entity> SelectedEntity;

		// Where FindEntity() last found each guid. Anyone can change the registry (GetRegistry(),
		// Open, New), so an entry is only trusted after checking that its entity is alive and still
		// has that guid; otherwise the registry is searched again.
		mutable std::unordered_map<Nyx::EntityGuid, Nyx::Entity> FoundEntities;
	};
}
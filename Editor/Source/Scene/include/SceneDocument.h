#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "NameComponent.h"
#include "SceneOutline.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

		// The selected entity that the details panel, the gizmo and so on act on: the last of
		// GetSelectedEntities()
		const std::optional<Nyx::Entity>& GetSelection() const { return SelectedEntity; }

		// All the selected entities, as the outliner selects several
		const std::vector<Nyx::Entity>& GetSelectedEntities() const { return SelectedEntities; }
		bool IsSelected(Nyx::Entity entity) const;

		// Selects the entity alone, or nothing
		void Select(std::optional<Nyx::Entity> entity);
		void SelectEntities(std::vector<Nyx::Entity> entities);

		// The selection by guid, and back: undo and redo can bring an entity back in another slot.
		// What is gone by then is no longer selected.
		std::vector<Nyx::EntityGuid> GetSelectedGuids() const;
		void SelectGuids(const std::vector<Nyx::EntityGuid>& guids);

		// The outliner's folders and order, saved with the scene (SceneOutlineEdits.h). It may list
		// entities that are gone, e.g. deleted ones, so that undo brings them back to their place.
		Nyx::SceneOutline& GetOutline() { return Outline; }
		const Nyx::SceneOutline& GetOutline() const { return Outline; }

	private:
		Nyx::Registry Registry;
		Nyx::SceneOutline Outline;
		std::optional<Nyx::Entity> SelectedEntity;
		std::vector<Nyx::Entity> SelectedEntities;

		// Where FindEntity() last found each guid. Anyone can change the registry (GetRegistry(),
		// Open, New), so an entry is only trusted after checking that its entity is alive and still
		// has that guid; otherwise the registry is searched again.
		mutable std::unordered_map<Nyx::EntityGuid, Nyx::Entity> FoundEntities;
	};
}
#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "NameComponent.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace Nyx::Engine
{
	class Registry;
}

namespace Nyx
{
	class SceneDocument
	{
	public:
		SceneDocument() = default;

		Nyx::Engine::Registry& GetRegistry() { return Registry; }
		const Nyx::Engine::Registry& GetRegistry() const { return Registry; }

		Nyx::Engine::Entity CreateEntity(const std::string& name = "Entity");
		bool DestroyEntity(Nyx::Engine::Entity entity);

		// The live entity with this guid. Undo steps name entities by guid, since a handle's slot is
		// reused once its entity is deleted, e.g. by the next entity added.
		std::optional<Nyx::Engine::Entity> FindEntity(Nyx::Engine::EntityGuid guid) const;

		// The entity's guid; invalid if it has none
		Nyx::Engine::EntityGuid GetGuid(Nyx::Engine::Entity entity) const;

		std::optional<Nyx::Engine::Entity>& GetSelection() { return SelectedEntity; }
		const std::optional<Nyx::Engine::Entity>& GetSelection() const { return SelectedEntity; }

	private:
		Nyx::Engine::Registry Registry;
		std::optional<Nyx::Engine::Entity> SelectedEntity;

		// Where FindEntity() last found each guid. Anyone can change the registry (GetRegistry(),
		// Open, New), so an entry is only trusted after checking that its entity is alive and still
		// has that guid; otherwise the registry is searched again.
		mutable std::unordered_map<Nyx::Engine::EntityGuid, Nyx::Engine::Entity> FoundEntities;
	};
}
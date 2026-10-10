#include "EditorPCH.h"
#include "SceneDocument.h"
#include "Entity.h"
#include "GuidComponent.h"

namespace Nyx
{
	Nyx::Engine::Entity SceneDocument::CreateEntity(const std::string& name)
	{
		Nyx::Engine::Entity entity = Registry.CreateEntity();
		Registry.Add<Nyx::Engine::NameComponent>(entity, Nyx::Engine::NameComponent{ name });

		// The entities of a scene are saved, so they need a guid that stays the same in every file and process
		Registry.Add<Nyx::Engine::GuidComponent>(entity, Nyx::Engine::GuidComponent{ Nyx::Engine::EntityGuid::Generate() });
		return entity;
	}

	bool SceneDocument::DestroyEntity(Nyx::Engine::Entity entity)
	{
		if (SelectedEntity.has_value() && SelectedEntity.value() == entity)
		{
			SelectedEntity.reset();
		}

		return Registry.DestroyEntity(entity);
	}

	std::optional<Nyx::Engine::Entity> SceneDocument::FindEntity(Nyx::Engine::EntityGuid guid) const
	{
		if (!guid.IsValid())
		{
			return std::nullopt;
		}

		const auto found = FoundEntities.find(guid);
		if (found != FoundEntities.end() && GetGuid(found->second) == guid)
		{
			return found->second;
		}

		// Scenes are small; a search is cheap next to the edit that needs it
		std::optional<Nyx::Engine::Entity> entity;
		Registry.ForEachEntity([&](Nyx::Engine::Entity candidate)
			{
				if (!entity && GetGuid(candidate) == guid)
				{
					entity = candidate;
				}
			});

		if (entity)
		{
			FoundEntities[guid] = *entity;
		}
		else
		{
			FoundEntities.erase(guid);
		}

		return entity;
	}

	Nyx::Engine::EntityGuid SceneDocument::GetGuid(Nyx::Engine::Entity entity) const
	{
		if (!Registry.IsAlive(entity) || !Registry.Has<Nyx::Engine::GuidComponent>(entity))
		{
			return {};
		}

		return Registry.Get<Nyx::Engine::GuidComponent>(entity).Guid;
	}
}
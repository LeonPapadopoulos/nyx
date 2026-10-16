#include "EditorPCH.h"
#include "SceneDocument.h"
#include "Entity.h"
#include "GuidComponent.h"

namespace Nyx::Editor
{
	Nyx::Entity SceneDocument::CreateEntity(const std::string& name)
	{
		Nyx::Entity entity = Registry.CreateEntity();
		Registry.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ name });

		// The entities of a scene are saved, so they need a guid that stays the same in every file and process
		Registry.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ Nyx::EntityGuid::Generate() });
		return entity;
	}

	bool SceneDocument::DestroyEntity(Nyx::Entity entity)
	{
		if (SelectedEntity.has_value() && SelectedEntity.value() == entity)
		{
			SelectedEntity.reset();
		}

		return Registry.DestroyEntity(entity);
	}

	std::optional<Nyx::Entity> SceneDocument::FindEntity(Nyx::EntityGuid guid) const
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
		std::optional<Nyx::Entity> entity;
		Registry.ForEachEntity([&](Nyx::Entity candidate)
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

	Nyx::EntityGuid SceneDocument::GetGuid(Nyx::Entity entity) const
	{
		if (!Registry.IsAlive(entity) || !Registry.Has<Nyx::GuidComponent>(entity))
		{
			return {};
		}

		return Registry.Get<Nyx::GuidComponent>(entity).Guid;
	}
}
#include "EditorPCH.h"
#include "SceneDocument.h"
#include "Entity.h"
#include "GuidComponent.h"

#include <algorithm>
#include <utility>

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
		if (IsSelected(entity))
		{
			std::vector<Nyx::Entity> others = SelectedEntities;
			std::erase(others, entity);
			SelectEntities(std::move(others));
		}

		return Registry.DestroyEntity(entity);
	}

	bool SceneDocument::IsSelected(Nyx::Entity entity) const
	{
		return std::find(SelectedEntities.begin(), SelectedEntities.end(), entity) != SelectedEntities.end();
	}

	void SceneDocument::Select(std::optional<Nyx::Entity> entity)
	{
		SelectEntities(entity ? std::vector<Nyx::Entity>{ *entity } : std::vector<Nyx::Entity>{});
	}

	void SceneDocument::SelectEntities(std::vector<Nyx::Entity> entities)
	{
		SelectedEntities = std::move(entities);
		SelectedEntity = SelectedEntities.empty() ? std::nullopt : std::optional<Nyx::Entity>(SelectedEntities.back());
	}

	std::vector<Nyx::EntityGuid> SceneDocument::GetSelectedGuids() const
	{
		std::vector<Nyx::EntityGuid> guids;
		for (const Nyx::Entity entity : SelectedEntities)
		{
			if (const Nyx::EntityGuid guid = GetGuid(entity); guid.IsValid())
			{
				guids.push_back(guid);
			}
		}
		return guids;
	}

	void SceneDocument::SelectGuids(const std::vector<Nyx::EntityGuid>& guids)
	{
		std::vector<Nyx::Entity> entities;
		for (const Nyx::EntityGuid guid : guids)
		{
			if (const std::optional<Nyx::Entity> entity = FindEntity(guid))
			{
				entities.push_back(*entity);
			}
		}
		SelectEntities(std::move(entities));
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
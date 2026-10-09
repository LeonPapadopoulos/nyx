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
}
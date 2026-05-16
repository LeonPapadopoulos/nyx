#include "NyxPCH.h"
#include "SceneDocument.h"
#include "Entity.h"

namespace Nyx
{
	Nyx::Engine::Entity SceneDocument::CreateEntity(const std::string& name)
	{
		Nyx::Engine::Entity entity = Registry.CreateEntity();
		Registry.Add<Nyx::Engine::NameComponent>(entity, Nyx::Engine::NameComponent{ name });
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
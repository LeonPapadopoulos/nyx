#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "SceneDocument.h"
#include "TransactionObjectRef.h"

namespace Nyx::Editor
{
	// A scene entity in an undo step, named by its guid. Not by its handle: once the entity is
	// deleted, the next entity added can take its slot, and undoing the delete then has to bring
	// it back elsewhere. Invalid for an entity without a guid; such an edit isn't recorded.
	inline ObjectRef MakeSceneEntityRef(Nyx::Engine::EntityGuid guid)
	{
		return ObjectRef{
			.Domain = EObjectDomain::SceneEntity,
			.Id = InspectorTargetId{ guid.Value }
		};
	}

	inline ObjectRef MakeSceneEntityRef(const Nyx::SceneDocument& scene, Nyx::Engine::Entity entity)
	{
		return MakeSceneEntityRef(scene.GetGuid(entity));
	}

	inline Nyx::Engine::EntityGuid GetSceneEntityGuid(const ObjectRef& ref)
	{
		return ref.Domain == EObjectDomain::SceneEntity ? Nyx::Engine::EntityGuid{ ref.Id.Value } : Nyx::Engine::EntityGuid{};
	}
}

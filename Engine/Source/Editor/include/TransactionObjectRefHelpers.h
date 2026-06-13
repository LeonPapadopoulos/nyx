#pragma once

#include "Entity.h"
#include "TransactionObjectRef.h"

namespace Nyx::Editor
{
	inline ObjectRef MakeSceneEntityRef(Nyx::Engine::Entity entity)
	{
		return ObjectRef{
			.Domain = EObjectDomain::SceneEntity,
			.Id = InspectorTargetId{ static_cast<uint64_t>(entity.Value) }
		};
	}
}
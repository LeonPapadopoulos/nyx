#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <cstdint>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta = (DisplayName = "Guid Property Test"))
	struct GuidPropertyTestComponent
	{
		// An EntityGuid is reflected as the UInt64 inside it
		NYX_PROPERTY(Serialize, ReadOnly)
		EntityGuid Guid;

		NYX_PROPERTY(Serialize, ReadOnly)
		Nyx::Engine::EntityGuid QualifiedGuid;

		NYX_PROPERTY(Edit, Serialize)
		uint64_t Big = 0;
	};
}

#include "Generated/Runtime/Input.reflect.h"

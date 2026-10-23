#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <cstdint>

namespace Nyx
{
	NYX_REFLECT(Component, meta = (DisplayName = "Guid Property Test"))
	struct GuidPropertyTestComponent
	{
		// An EntityGuid is reflected as the UInt64 inside it, with UI = EntityRef
		NYX_PROPERTY(Serialize, ReadOnly)
		EntityGuid Guid;

		NYX_PROPERTY(Serialize, ReadOnly)
		Nyx::EntityGuid QualifiedGuid;

		// Names its own UI, so it doesn't get EntityRef
		NYX_PROPERTY(Serialize, ReadOnly, meta = (UI = Hex, Tooltip = "Its own"))
		EntityGuid OwnGuid;

		NYX_PROPERTY(Edit, Serialize)
		uint64_t Big = 0;
	};
}

#include "Generated/Runtime/Input.reflect.h"

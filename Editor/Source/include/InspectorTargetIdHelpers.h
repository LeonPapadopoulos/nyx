#pragma once

#include "InspectorTargetId.h"
#include "Entity.h"

#include <cstdint>

namespace Nyx::Editor
{
	inline InspectorTargetId MakeInspectorTargetId(Nyx::Engine::Entity entity)
	{
		return InspectorTargetId{ static_cast<uint64_t>(entity.Value) };
	}

	inline InspectorTargetId MakeInspectorTargetId(const void* objectPtr)
	{
		return InspectorTargetId{ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(objectPtr)) };
	}
}
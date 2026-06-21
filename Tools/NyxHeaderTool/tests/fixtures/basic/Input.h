#pragma once

#include "ReflectionMacros.h"
#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta(DisplayName = "Name"))
	struct NameComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta(DisplayName = "Name", Category = "Identity", Tooltip = "Entity display name"))
		std::string Name;
	};
}

#include "Generated/Runtime/Input.reflect.h"
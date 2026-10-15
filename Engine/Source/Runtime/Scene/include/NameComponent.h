#pragma once

#include "ReflectionMacros.h"

#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta = (DisplayName = "Name Component"))
	struct NameComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;
	};
}

#include "Generated/Runtime/NameComponent.reflect.h"
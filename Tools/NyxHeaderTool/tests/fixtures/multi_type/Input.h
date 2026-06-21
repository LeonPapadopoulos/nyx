#pragma once

#include "ReflectionMacros.h"
#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta(DisplayName = "Name"))
	struct NameComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta(DisplayName = "Name"))
		std::string Name;
	};

	NYX_REFLECT(Component, meta(DisplayName = "Dummy Name"))
	struct DummyNameComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta(DisplayName = "Dummy Name"))
		std::string DummyName;
	};
}

#include "Generated/Runtime/Input.reflect.h"
#pragma once

#include "ReflectionMacros.h"

namespace Sample
{
	NYX_REFLECT(Component, meta = (DisplayName = "Lantern"))
	struct LanternComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.1))
		float Brightness = 1.0f;
	};
}

#include "Generated/Sample/Input.reflect.h"

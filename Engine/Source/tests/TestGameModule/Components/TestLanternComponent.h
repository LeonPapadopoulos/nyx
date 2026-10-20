#pragma once

#include "ReflectionMacros.h"

#include <string>

// A component of the test game module (NyxTestModule), built the way a game builds its own: in a
// folder of the module's, outside the engine, through nyx_add_game_module().
namespace NyxTestModule
{
	NYX_REFLECT(Component, meta = (DisplayName = "Test Lantern"))
	struct TestLanternComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.1))
		float Brightness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light"))
		std::string Label;
	};
}

#include "Generated/NyxTestModule/TestLanternComponent.reflect.h"

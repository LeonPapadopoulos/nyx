#pragma once

#include "ValueOverride.h"
#include "ReflectionMacros.h"

#include <string>
#include <vector>

// A component of the test game module (NyxTestModule), built the way a game builds its own: in a
// folder of the module's, outside the engine, through nyx_add_game_module(). Its values follow its
// shade, as a game's follow its data files, except those it overrides (the module's provider lists
// them).
namespace NyxTestModule
{
	NYX_REFLECT(Component, meta = (DisplayName = "Test Lantern"))
	struct TestLanternComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.1))
		float Brightness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light"))
		std::string Label;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shade", Tooltip = "Warm or Cold: which values it inherits"))
		std::string Shade = "Warm";

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shade", DisplayName = "Values", UI = Overrides))
		std::vector<Nyx::ValueOverride> Overrides;
	};
}

#include "Generated/NyxTestModule/TestLanternComponent.reflect.h"

#pragma once

#include "ValueOverride.h"
#include "ReflectionMacros.h"
#include "SurfaceAnchor.h"

#include <vector>

// A component of the test game module that holds one of the engine's reflected structs, as a game's
// component holds a SurfaceAnchor to hang something on another entity's surface. Its overrides have
// no provider, so they are drawn as any array.
namespace NyxTestModule
{
	NYX_REFLECT(Component, meta = (DisplayName = "Test Marker"))
	struct TestMarkerComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Marker"))
		Nyx::SurfaceAnchor Anchor;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Marker", UI = Overrides))
		std::vector<Nyx::ValueOverride> Overrides;
	};
}

#include "Generated/NyxTestModule/TestMarkerComponent.reflect.h"

#pragma once

#include "ReflectionMacros.h"
#include "SurfaceAnchor.h"

// A component of the test game module that holds one of the engine's reflected structs, as a game's
// component holds a SurfaceAnchor to hang something on another entity's surface.
namespace NyxTestModule
{
	NYX_REFLECT(Component, meta = (DisplayName = "Test Marker"))
	struct TestMarkerComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Marker"))
		Nyx::SurfaceAnchor Anchor;
	};
}

#include "Generated/NyxTestModule/TestMarkerComponent.reflect.h"

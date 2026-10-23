#pragma once

#include "ReflectionMacros.h"

namespace Nyx
{
	// The game flies this entity freely with the fly camera's input (UpdateFlyCameras(),
	// FlyCameras.h): W/A/S/D and the left stick move, E/Q and the triggers go up and down, the
	// right stick turns, the right mouse button held looks around with the mouse, the wheel changes
	// the speed. A scene without one has no fly camera, so a game's own camera has its input.
	NYX_REFLECT(Component, meta = (DisplayName = "Fly Camera Component"))
	struct FlyCameraComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fly Camera", DisplayName = "Move Speed", DragSpeed = 0.1, Tooltip = "Metres per second; the wheel multiplies or divides it by 1.2 per notch while the game runs"))
		float MoveSpeed = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fly Camera", DisplayName = "Look Sensitivity", DragSpeed = 0.01, Tooltip = "How fast looking around turns it; 1 turns as the input's bindings say"))
		float LookSensitivity = 1.0f;

		// While the game runs, not saved: what the wheel made of the speed, and whether the right
		// mouse button looks around
		float SpeedScale = 1.0f;
		bool bLooking = false;
	};
}

#include "Generated/Runtime/FlyCameraComponent.reflect.h"

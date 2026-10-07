#pragma once

#include "ReflectionMacros.h"

namespace Nyx::Engine
{
	// A camera the scene can be viewed through, looking along the entity's forward axis (-Z).
	// The game uses the first camera marked as primary.
	NYX_REFLECT(Component, meta = (DisplayName = "Camera Component"))
	struct CameraComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Camera", DisplayName = "Field of View", Tooltip = "Vertical field of view, in degrees"))
		float FovYDegrees = 60.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Camera", DragSpeed = 0.01, Tooltip = "Anything closer than this is not rendered"))
		float NearPlane = 0.1f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Camera", Tooltip = "Anything farther away than this is not rendered"))
		float FarPlane = 1000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Camera", DisplayName = "Primary", Tooltip = "Whether the game views the scene through this camera"))
		bool bPrimary = true;
	};
}

#include "Generated/Runtime/CameraComponent.reflect.h"
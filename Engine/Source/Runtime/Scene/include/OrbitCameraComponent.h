#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// A camera that circles an entity, e.g. the character, looking at a point above it, and follows it
	// where it is drawn (UpdateOrbitCameras(), OrbitCameras.h). What turns and zooms it is filled in
	// by its caller each frame (LookDelta, ZoomDelta); it reads no input itself. It starts from where
	// the camera looks, as placed in the scene, at the Distance set here.
	NYX_REFLECT(Component, meta = (DisplayName = "Orbit Camera"))
	struct OrbitCameraComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Orbit Camera", Tooltip = "The entity it circles and follows"))
		EntityGuid Target;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Orbit Camera", DisplayName = "Target Offset", DragSpeed = 0.01, Tooltip = "Added to the target's position, in world space: the point it looks at, e.g. the shoulders"))
		glm::vec3 TargetOffset{ 0.0f, 1.5f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distance", DragSpeed = 0.05, Tooltip = "From the point it looks at, in metres, until zoomed"))
		float Distance = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distance", DisplayName = "Min Distance", DragSpeed = 0.05))
		float MinDistance = 1.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distance", DisplayName = "Max Distance", DragSpeed = 0.05))
		float MaxDistance = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distance", DisplayName = "Zoom Step", DragSpeed = 0.01, Tooltip = "Metres closer or farther for each step of zoom, e.g. a notch of the wheel"))
		float ZoomStep = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Angles", DisplayName = "Min Pitch", DragSpeed = 0.5, Tooltip = "In degrees, below 0 looking down: how far over the target it goes"))
		float MinPitchDegrees = -75.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Angles", DisplayName = "Max Pitch", DragSpeed = 0.5, Tooltip = "In degrees, above 0 looking up: how far under the target's eyes it goes"))
		float MaxPitchDegrees = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Angles", DisplayName = "Look Sensitivity", DragSpeed = 0.01, Tooltip = "How fast the look input turns it; 1 turns as the input says"))
		float LookSensitivity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Orbit Camera", DisplayName = "Follow Smoothing", DragSpeed = 0.01, Tooltip = "Seconds it takes to close about two thirds of the way to where the target went; 0 follows it exactly"))
		float FollowSmoothing = 0.1f;

		// While the game runs, not saved. This frame's input, filled in by the caller and cleared by
		// UpdateOrbitCameras(): radians to turn (x to the right, y up) and steps to zoom (in > 0)
		glm::vec2 LookDelta{ 0.0f };
		float ZoomDelta = 0.0f;

		// Where it is: turned Yaw radians about up from looking along -Z (to the left > 0), Pitch
		// radians up from level, CurrentDistance from the point it looks at, which follows the target
		float Yaw = 0.0f;
		float Pitch = 0.0f;
		float CurrentDistance = 0.0f;
		glm::vec3 FollowPoint{ 0.0f };
		// Whether it has taken its angles from the camera yet, and the Distance it last took, so a
		// live edit of Distance counts
		bool bStarted = false;
		float AppliedDistance = 0.0f;
	};

	// The way a camera turned `yaw` looks, flattened onto the ground (y = 0), and its right there:
	// for walking relative to the camera
	glm::vec3 GetGroundForward(float yaw);
	glm::vec3 GetGroundRight(float yaw);

	// A stick's or W/A/S/D's input (x to the right, y forward, each up to 1) as a move along the
	// ground relative to a camera turned `yaw`, at most 1 long: e.g. CharacterComponent::DesiredMove
	glm::vec3 GetGroundMove(float yaw, const glm::vec2& input);
}

#include "Generated/Runtime/OrbitCameraComponent.reflect.h"

#pragma once

#include "Entity.h"

#include <optional>

namespace Nyx
{
	class PhysicsWorld;

	// The world's orbit cameras (OrbitCameraComponent), once per frame after the frame's fixed steps
	// (ILayer::OnLateUpdate()): each turns by its LookDelta and zooms by its ZoomDelta, clearing them,
	// keeps its pitch and distance in their limits, and is put on its target where the target is
	// drawn, `interpolation` of the way between its last two steps (GetDrawnTransform(); the factor
	// the renderer draws the world by, FixedStepClock::GetDrawFactor(), none if it draws transforms
	// as they are), so it doesn't judder against a character moving in steps. A camera whose target
	// isn't in the world is left alone. A camera without a TransformComponent is skipped.
	//
	//
	// With the game world's physics, it keeps out of what is fixed in the world, the terrain's
	// collision too: a ball of ProbeRadius cast from the point it looks at towards where it would be
	// (PhysicsWorld::ShapeCast(), static bodies only, so neither its target nor what moves pushes it)
	// puts it in front of what the ball hits, at once, and it goes back out to its distance over
	// about ReturnSeconds once that is clear.
	void UpdateOrbitCameras(Registry& world, float deltaTime, std::optional<float> interpolation, const PhysicsWorld* physics = nullptr);

	namespace OrbitCameraLimits
	{
		// In metres: a little more than a camera's near plane, so the picture never cuts into a wall
		constexpr float ProbeRadius = 0.2f;
		// About two thirds of the way back out in this many seconds
		constexpr float ReturnSeconds = 0.25f;
	}
}

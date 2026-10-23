#pragma once

#include "Entity.h"

#include <optional>

namespace Nyx
{
	// The world's orbit cameras (OrbitCameraComponent), once per frame after the frame's fixed steps
	// (ILayer::OnLateUpdate()): each turns by its LookDelta and zooms by its ZoomDelta, clearing them,
	// keeps its pitch and distance in their limits, and is put on its target where the target is
	// drawn, `interpolation` of the way between its last two steps (GetDrawnTransform(); the factor
	// the renderer draws the world by, FixedStepClock::GetDrawFactor(), none if it draws transforms
	// as they are), so it doesn't judder against a character moving in steps. A camera whose target
	// isn't in the world is left alone. A camera without a TransformComponent is skipped.
	//
	// Walls aren't handled yet: the camera can end up behind one, or inside it. When play shows it's
	// needed, cast a sphere of about the near plane's size from the point it looks at towards where
	// it would be (PhysicsWorld::ShapeCast(), leaving out the target's body), and put it at the hit
	// distance when that is nearer, coming back out smoothly. That needs the game world's physics,
	// which is why this lives in Sim.
	void UpdateOrbitCameras(Registry& world, float deltaTime, std::optional<float> interpolation);
}

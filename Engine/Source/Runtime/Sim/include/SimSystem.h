#pragma once

#include "DebugDraw.h"
#include "Entity.h"
#include "FixedStepClock.h"
#include "PhysicsBodies.h"
#include "PhysicsWorld.h"

#include <cstdint>

namespace Nyx
{
	// What a system gets for one simulation step
	struct SimContext
	{
		// The world the step changes
		Registry& World;
		// Which step this is, and how long
		const StepTime& Step;
		// Lines over the world, to see what the system does; shown until the next step, or for the
		// seconds given (DebugDraw)
		DebugDraw& Debug;
		// The world's bodies: forces, velocities, poses. A force added before the physics step acts
		// during it.
		PhysicsWorld& Physics;
		// Which body is whose (FindBody(), FindEntity())
		const PhysicsBodies& Bodies;
	};

	// Where in a step a system runs: before the physics step, e.g. to push bodies, or after it,
	// e.g. to react to where they ended up
	enum class ESimStage : uint8_t
	{
		BeforePhysics,
		AfterPhysics
	};

	// One part of the simulation, run by the GameWorld in its place in the order, every step or every
	// nth one. Systems talk through components, not by calling each other.
	class ISimSystem
	{
	public:
		virtual ~ISimSystem() = default;

		// For logs and the profiler, e.g. "Lantern Flicker"
		virtual const char* GetName() const = 0;

		virtual void Step(const SimContext& context) = 0;
	};
}

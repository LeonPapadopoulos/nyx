#pragma once

#include "CommandQueue.h"
#include "DebugDraw.h"
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "FixedStepClock.h"
#include "PhysicsBodies.h"
#include "PhysicsConstraints.h"
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
		// The world's entities by guid, for FindEntityByGuid(World, Guids, guid), e.g. to resolve
		// surface anchors. Made once a step, before the systems: an entity made or given another
		// guid during the step is found by its new guid from the next step on (by its old one until
		// then), and one destroyed is gone at once. A system that needs them sooner searches the
		// world (FindEntityByGuid(World, guid)) or makes a lookup of its own.
		const EntityGuidLookup& Guids;
		// What players asked for since the last step, in order (CommandQueue::ForEach<T>()). The
		// systems that own what a command changes apply it; a command nobody reads is dropped.
		const CommandQueue& Commands;
		// Which constraint is whose (FindConstraint()), and which broke in this step's physics step
		// (GetBrokenConstraints(), for ESimStage::AfterPhysics)
		const PhysicsConstraints& Constraints;
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

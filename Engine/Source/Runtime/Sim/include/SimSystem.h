#pragma once

#include "Entity.h"
#include "FixedStepClock.h"

namespace Nyx
{
	// What a system gets for one simulation step
	struct SimContext
	{
		// The world the step changes
		Registry& World;
		// Which step this is, and how long
		const StepTime& Step;
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

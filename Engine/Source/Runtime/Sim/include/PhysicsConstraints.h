#pragma once

#include "Entity.h"
#include "EntityGuidLookup.h"
#include "PhysicsBodies.h"
#include "PhysicsWorld.h"

#include <cstdint>
#include <map>
#include <vector>

namespace Nyx
{
	// A constraint that broke in the last step: the entity of its ConstraintComponent, and the load
	// that broke it
	struct ConstraintBrokenEvent
	{
		Entity Holder;
		ConstraintLoad Load;
	};

	// Keeps a PhysicsWorld's constraints in step with the entities' ConstraintComponents, each fixed
	// step:
	// - before the step, after the bodies (PhysicsBodies), a constraint is made once both its bodies
	//   are there, made again when its kind, frames, limits, spring or breaking change, when its
	//   entities change or their bodies are made again, and destroyed with its component or entity,
	//   or when a body goes. An entity that is a part of a group's body (BodyGroupComponent) is held
	//   by its frame in that body; two parts of the same body hold nothing between them, being one
	//   body. Switching it on or off, and its motor, reach it without making it again,
	//   so a system can drive a motor every step. Each one's entity gets a ConstraintStateComponent.
	// - after the step, each state takes what its constraint did; one that broke switches its
	//   component off and is listed (GetBrokenConstraints()) until the next step.
	// Made in the order of their entities, so the same scene gives the same constraints each run.
	class PhysicsConstraints
	{
	public:
		void SyncBeforeStep(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies, const EntityGuidLookup& guids);
		void ReadBackAfterStep(Registry& world, const PhysicsWorld& physics);

		// The constraint of the entity's ConstraintComponent; invalid if it has none now
		ConstraintHandle FindConstraint(Entity holder) const;
		size_t GetConstraintCount() const;

		// The constraints that broke in the last step, in the order of their entities
		const std::vector<ConstraintBrokenEvent>& GetBrokenConstraints() const { return Broken; }

	private:
		struct TrackedConstraint
		{
			Entity Owner;
			// Invalid while its bodies aren't both there
			ConstraintHandle Constraint;
			// Its values that make it, with its bodies' handles, to tell when it must be made again
			uint64_t DescriptionHash = 0;
			// What it was last told, to tell a change
			bool bEnabled = true;
			ConstraintMotor Motor;
			bool bSeen = false;
		};

		// By entity, in a fixed order
		std::map<uint64_t, TrackedConstraint> Constraints;
		std::vector<ConstraintBrokenEvent> Broken;
	};
}

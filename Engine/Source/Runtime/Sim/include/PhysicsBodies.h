#pragma once

#include "Entity.h"
#include "PhysicsWorld.h"

#include <cstdint>
#include <map>
#include <optional>

namespace Nyx
{
	// Keeps a PhysicsWorld's bodies in step with the entities' physics components
	// (PhysicsComponents.h), each fixed step:
	// - before the step, bodies are made for new colliders, made again when a component's values or
	//   the entity's scale changed, and destroyed with their collider or entity. A transform set from
	//   outside since the last step, by a system or an editor's live edit, teleports its body and
	//   stops it; a kinematic body is moved to its transform over the step.
	// - after the step, dynamic bodies write where they are into their transforms.
	class PhysicsBodies
	{
	public:
		void SyncBeforeStep(Registry& world, PhysicsWorld& physics, float seconds);
		void WriteBackAfterStep(Registry& world, PhysicsWorld& physics);

		// The entity's body; invalid if it has none
		BodyHandle FindBody(Entity entity) const;
		// The body's entity, for a body these bodies made
		std::optional<Entity> FindEntity(const PhysicsWorld& physics, BodyHandle body) const;

		size_t GetBodyCount() const { return Bodies.size(); }

	private:
		struct TrackedBody
		{
			Entity Owner;
			BodyHandle Body;
			// The body's values besides its pose, to tell when it must be made again
			uint64_t DescriptionHash = 0;
			bool bKinematic = false;
			bool bDynamic = false;
			// Where the body was put or left last, to tell a transform set from outside
			BodyPose LastPose;
			bool bSeen = false;
		};

		// By entity, in a fixed order, so bodies are made and destroyed the same way each time
		std::map<uint64_t, TrackedBody> Bodies;
	};
}

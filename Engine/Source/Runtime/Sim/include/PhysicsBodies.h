#pragma once

#include "Entity.h"
#include "PhysicsWorld.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace Nyx
{
	// Keeps a PhysicsWorld's bodies in step with the entities' physics components
	// (PhysicsComponents.h), each fixed step:
	// - before the step, bodies are made for new colliders, made again when a component's values or
	//   the entity's scale changed, and destroyed with their collider or entity. A transform set from
	//   outside since the last step, by a system or an editor's live edit, teleports its body and
	//   stops it; a kinematic body is moved to its transform over the step.
	// - after the step, dynamic bodies write where they are into their transforms, and those that
	//   moved where they were before into a PreviousTransformComponent, to be drawn between the two.
	//   A teleported body has none until it moves again. A kinematic body whose transform a system
	//   moved during the step (ESimStage::BeforePhysics) gets one too, from where its transform was
	//   as the step began; one moved between steps, by a live edit, is drawn where it was put.
	// Characters (CharacterComponent) the same way: made with the component, made again when its
	// shape changes, destroyed with it or the entity; a position set from outside teleports them. They
	// walk in MoveCharacters(), before the physics step.
	class PhysicsBodies
	{
	public:
		void SyncBeforeStep(Registry& world, PhysicsWorld& physics, float seconds);
		// After the systems of ESimStage::BeforePhysics, which say where the characters want to go,
		// and before the physics step: walks each character by its component, writes where it got to
		// into its transform and where it was before into its PreviousTransformComponent, and what it
		// stands on into its component
		void MoveCharacters(Registry& world, PhysicsWorld& physics, float seconds);
		void WriteBackAfterStep(Registry& world, PhysicsWorld& physics);

		// The entity's body, a character's the one inside it; invalid if it has none
		BodyHandle FindBody(Entity entity) const;
		// The body's entity, for a body these bodies made or a character's
		std::optional<Entity> FindEntity(const PhysicsWorld& physics, BodyHandle body) const;
		// The entity's character; invalid if it has none
		CharacterHandle FindCharacter(Entity entity) const;

		size_t GetBodyCount() const { return Bodies.size(); }
		size_t GetCharacterCount() const { return Characters.size(); }

	private:
		struct TrackedBody
		{
			Entity Owner;
			BodyHandle Body;
			// The body's values besides its pose, to tell when it must be made again
			uint64_t DescriptionHash = 0;
			bool bKinematic = false;
			bool bDynamic = false;
			// Where the body was put or left last, to tell a transform set from outside; a kinematic
			// body's transform as the step began
			BodyPose LastPose;
			bool bSeen = false;
		};

		struct TrackedCharacter
		{
			Entity Owner;
			CharacterHandle Character;
			// The kinematic body inside it, which rays and bodies meet
			BodyHandle Body;
			// Its shape's values, to tell when it must be made again
			uint64_t DescriptionHash = 0;
			// Where it was put or left last, to tell a position set from outside, and its rotation
			// then, which it is drawn turning from
			glm::vec3 LastPosition{ 0.0f };
			glm::quat LastRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			bool bSeen = false;
		};

		void SyncCharacters(Registry& world, PhysicsWorld& physics);

		// Warns once for each entity with a rigid body but no collider, which has no body: easy to
		// miss, since nothing else shows it
		void WarnOfRigidBodiesWithoutCollider(const Registry& world);

		// By entity, in a fixed order, so bodies are made and destroyed the same way each time
		std::map<uint64_t, TrackedBody> Bodies;
		std::map<uint64_t, TrackedCharacter> Characters;
		// The entities warned of; one leaves it once it has a collider, so a later loss warns again
		std::set<uint64_t> WarnedWithoutCollider;
	};
}

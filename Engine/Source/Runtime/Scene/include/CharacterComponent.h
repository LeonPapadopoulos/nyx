#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// An upright capsule that walks: standing on the entity's position, its feet there, it walks
	// where DesiredMove says, climbs steps, slides down ground too steep, follows the ground down,
	// jumps when asked and pushes light bodies aside. PhysicsBodies moves it each fixed step and
	// writes the entity's position; the rotation is the game's, e.g. to face where it walks, and
	// doesn't turn the capsule. It needs a TransformComponent. Colliders on the same entity are left
	// out: the capsule is its shape.
	NYX_REFLECT(Component, meta = (DisplayName = "Character"))
	struct CharacterComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.01, Tooltip = "Of the capsule, in metres"))
		float Radius = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.01, Tooltip = "From the feet to the top of the head, in metres; at least twice the radius"))
		float Height = 1.8f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", DisplayName = "Max Slope", DragSpeed = 0.5, Tooltip = "In degrees: steeper ground it can't walk up, and slides down"))
		float MaxSlopeDegrees = 45.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", DragSpeed = 0.01, Tooltip = "The highest step it climbs, in metres"))
		float StepHeight = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", DragSpeed = 0.01, Tooltip = "How far down it follows the ground instead of leaving it, e.g. going down stairs, in metres"))
		float GroundSnapDistance = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Movement", DragSpeed = 0.1, Tooltip = "Metres per second, with the desired move at full length"))
		float WalkSpeed = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Movement", DragSpeed = 0.1, Tooltip = "How fast it gets to the speed it wants on the ground, in m/s per second"))
		float Acceleration = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Movement", DisplayName = "Air Control", DragSpeed = 0.01, Tooltip = "Its acceleration while in the air, as a part of the one on the ground: 0 can't steer, 1 steers as on the ground"))
		float AirControl = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Movement", DragSpeed = 0.1, Tooltip = "Upwards, in m/s; it rises about speed * speed / 20: 4 m/s, 0.8 m"))
		float JumpSpeed = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Movement", DragSpeed = 0.01, Tooltip = "1: falls normally, 0: floats"))
		float GravityFactor = 1.0f;

		// While the game runs, not saved. What the player or the game wants, simulation input: set
		// by a system of ESimStage::BeforePhysics, e.g. from a player's command, never from outside
		// a step. Where to walk, in world space along the ground, up to 1 long (the walk speed); a
		// jump, taken by this step whether or not it can jump then
		glm::vec3 DesiredMove{ 0.0f };
		bool bJumpRequested = false;

		// What the last step left: whether it stands on walkable ground, which way that faces (up in
		// the air), and how fast it goes
		bool bOnGround = false;
		glm::vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Velocity{ 0.0f };
	};
}

#include "Generated/Runtime/CharacterComponent.reflect.h"

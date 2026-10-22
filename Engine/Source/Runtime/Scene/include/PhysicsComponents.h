#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

// What makes an entity a physics body. A collider alone makes a static body, which others bump into
// and which never moves; with a RigidBodyComponent too, the body moves. One collider an entity:
// with more, the box is taken over the sphere, and the sphere over the capsule. The entity's
// TransformComponent places the body, and its scale scales the shape.
namespace Nyx
{
	NYX_REFLECT(Component, meta = (DisplayName = "Box Collider"))
	struct BoxColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Half the box's size along each axis, before the entity's scale"))
		glm::vec3 HalfExtents{ 0.5f };
	};

	NYX_REFLECT(Component, meta = (DisplayName = "Sphere Collider"))
	struct SphereColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Before the entity's scale; the largest axis of the scale counts"))
		float Radius = 0.5f;
	};

	// Upright along the entity's Y axis
	NYX_REFLECT(Component, meta = (DisplayName = "Capsule Collider"))
	struct CapsuleColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Before the entity's scale; the larger of its X and Z counts"))
		float Radius = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Half the length of the middle, without the round ends; scaled by the entity's Y"))
		float HalfHeight = 0.5f;
	};

	// The body moves: by forces, gravity and collisions, or, kinematic, only where its transform is set
	NYX_REFLECT(Component, meta = (DisplayName = "Rigid Body"))
	struct RigidBodyComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", Tooltip = "Moves only where its transform is set, pushing what it meets; nothing pushes it"))
		bool bKinematic = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.1, Tooltip = "In kg; 0: from the shape's volume at the density of water"))
		float Mass = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01, Tooltip = "Velocity lost per second, roughly"))
		float LinearDamping = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01))
		float AngularDamping = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01, Tooltip = "1: falls normally, 0: floats"))
		float GravityFactor = 1.0f;
	};

	// How a body's surface slides and bounces; without one, the defaults
	NYX_REFLECT(Component, meta = (DisplayName = "Physics Material"))
	struct PhysicsMaterialComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Physics Material", DragSpeed = 0.01))
		float Friction = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Physics Material", DragSpeed = 0.01, Tooltip = "0: no bounce, 1: bounces back as fast as it came"))
		float Restitution = 0.0f;
	};
}

#include "Generated/Runtime/PhysicsComponents.reflect.h"

#pragma once

#include "AssetReference.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

// What makes an entity a physics body. A collider alone makes a static body, which others bump into
// and which never moves; with a RigidBodyComponent too, the body moves. One collider an entity:
// with more, the box is taken over the sphere, the sphere over the capsule, and the capsule over the
// mesh. The entity's TransformComponent places the body, and its scale scales the shape. The
// collider's physics material (a .nyxphysmat asset, PhysicsMaterials in Sim) gives the body its
// friction, restitution and density.
namespace Nyx
{
	NYX_REFLECT(Component, meta = (DisplayName = "Box Collider"))
	struct BoxColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Half the box's size along each axis, before the entity's scale"))
		glm::vec3 HalfExtents{ 0.5f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", AssetType = PhysicsMaterial, Tooltip = "How its surface slides and bounces, how heavy its stuff is, and the game's own numbers (.nyxphysmat); none: the engine's"))
		Nyx::AssetReference Material;
	};

	NYX_REFLECT(Component, meta = (DisplayName = "Sphere Collider"))
	struct SphereColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Before the entity's scale; the largest axis of the scale counts"))
		float Radius = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", AssetType = PhysicsMaterial, Tooltip = "How its surface slides and bounces, how heavy its stuff is, and the game's own numbers (.nyxphysmat); none: the engine's"))
		Nyx::AssetReference Material;
	};

	// Upright along the entity's Y axis
	NYX_REFLECT(Component, meta = (DisplayName = "Capsule Collider"))
	struct CapsuleColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Before the entity's scale; the larger of its X and Z counts"))
		float Radius = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.01, Tooltip = "Half the length of the middle, without the round ends; scaled by the entity's Y"))
		float HalfHeight = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", AssetType = PhysicsMaterial, Tooltip = "How its surface slides and bounces, how heavy its stuff is, and the game's own numbers (.nyxphysmat); none: the engine's"))
		Nyx::AssetReference Material;
	};

	// The triangles of the entity's mesh made at run time (RuntimeMeshComponent), e.g. a generated
	// ground, as a static body: what moves collides with it, characters walk on it, rays hit its
	// triangles from the front. Always static, also with a Rigid Body: Jolt's meshes can't move by
	// forces. Made again when the mesh's revision, its data or the entity's scale changes; that costs
	// about as much as making it, so a mesh that changes every step is better left without one.
	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Collider"))
	struct MeshColliderComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", DragSpeed = 0.1, Tooltip = "Where two triangles meet bending less than this, in degrees, the edge is smooth: what slides across it doesn't catch on it"))
		float SmoothEdgeDegrees = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Collider", AssetType = PhysicsMaterial, Tooltip = "How its surface slides and bounces, how heavy its stuff is, and the game's own numbers (.nyxphysmat); none: the engine's"))
		Nyx::AssetReference Material;
	};

	// The body moves: by forces, gravity and collisions, or, kinematic, only where its transform is set
	NYX_REFLECT(Component, meta = (DisplayName = "Rigid Body"))
	struct RigidBodyComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", Tooltip = "Moves only where its transform is set, pushing what it meets; nothing pushes it"))
		bool bKinematic = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.1, Tooltip = "In kg; 0: from the shape's volume at its physics material's density"))
		float Mass = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01, Tooltip = "Velocity lost per second, roughly"))
		float LinearDamping = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01))
		float AngularDamping = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rigid Body", DragSpeed = 0.01, Tooltip = "1: falls normally, 0: floats"))
		float GravityFactor = 1.0f;
	};

	// How one entity's surface slides and bounces, over its collider's physics material
	NYX_REFLECT(Component, meta = (DisplayName = "Physics Material"))
	struct PhysicsMaterialComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Physics Material", DragSpeed = 0.01))
		float Friction = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Physics Material", DragSpeed = 0.01, Tooltip = "0: no bounce, 1: bounces back as fast as it came"))
		float Restitution = 0.0f;
	};

	// Makes the entity's collider a part of one body with every other entity of the same Group, as a
	// game joins things into one rigid whole (PhysicsBodies): each keeps its collider, its surface,
	// its mass and its entity, and the body has their centre of mass and inertia together; each
	// entity's transform follows the body. Only dynamic rigid bodies join: a static, kinematic or
	// mesh collider keeps a body of its own, and a group of one entity is that entity's own body.
	// Runtime only: not reflected, so never saved, edited or sent; a game's system sets it.
	struct BodyGroupComponent
	{
		// 0: none
		uint64_t Group = 0;
	};
}

#include "Generated/Runtime/PhysicsComponents.reflect.h"

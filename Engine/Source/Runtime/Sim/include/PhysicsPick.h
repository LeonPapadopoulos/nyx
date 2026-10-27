#pragma once

#include "CameraProjection.h"
#include "Entity.h"
#include "PhysicsWorld.h"
#include "SurfaceAnchor.h"

#include <glm/glm.hpp>

#include <optional>

namespace Nyx
{
	class GameWorld;
	class PhysicsBodies;

	// What a ray hit first in a game world, such as the ray through the cursor (ScreenPointToRay())
	struct PhysicsPick
	{
		// The entity whose body was hit, the part's for a body made of several; null for a body no
		// entity owns, made outside PhysicsBodies
		Entity HitEntity;
		glm::vec3 Point{ 0.0f };
		// The surface's normal there, pointing back at the ray's origin
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		// From the ray's origin to Point
		float Distance = 0.0f;
	};

	// The first body along the ray, at most `maxDistance` away, such as the camera's far plane, and
	// the entity it belongs to. The filter leaves bodies out: the caster's own (Ignore), sensors
	// (unless bSensors), static or moving ones.
	std::optional<PhysicsPick> PickPhysicsBody(const PhysicsWorld& physics, const PhysicsBodies& bodies, const Ray& ray,
		float maxDistance, const QueryFilter& filter = {});
	std::optional<PhysicsPick> PickPhysicsBody(
		const GameWorld& world, const Ray& ray, float maxDistance, const QueryFilter& filter = {});

	// An anchor on the hit entity at the hit point and normal, which then moves with the entity, such
	// as for a mark placed by a click. Its tangent is GetSurfaceTangent()'s. A world-space anchor for
	// a hit without an entity.
	SurfaceAnchor MakeSurfaceAnchor(const Registry& world, const PhysicsPick& pick);
}

#include "NyxPCH.h"
#include "PhysicsPick.h"

#include "GameWorld.h"
#include "PhysicsBodies.h"

namespace Nyx
{
	std::optional<PhysicsPick> PickPhysicsBody(const PhysicsWorld& physics, const PhysicsBodies& bodies, const Ray& ray,
		float maxDistance, const QueryFilter& filter)
	{
		const std::optional<QueryHit> hit = physics.RayCast(ray.Origin, ray.Direction, maxDistance, filter);
		if (!hit)
		{
			return std::nullopt;
		}
		return PhysicsPick{ bodies.FindEntity(physics, *hit).value_or(Entity{}), hit->Point, hit->Normal, hit->Distance };
	}

	std::optional<PhysicsPick> PickPhysicsBody(
		const GameWorld& world, const Ray& ray, float maxDistance, const QueryFilter& filter)
	{
		return PickPhysicsBody(world.GetPhysics(), world.GetBodies(), ray, maxDistance, filter);
	}

	SurfaceAnchor MakeSurfaceAnchor(const Registry& world, const PhysicsPick& pick)
	{
		return MakeSurfaceAnchor(world, pick.HitEntity, pick.Point, pick.Normal, GetSurfaceTangent(pick.Normal));
	}
}

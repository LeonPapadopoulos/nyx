#include "NyxPCH.h"
#include "PhysicsBodies.h"

#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "PhysicsComponents.h"
#include "Profiler.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nyx
{
	namespace
	{
		// What a body is made from: its description, or nothing for an entity without a collider
		std::optional<BodyDesc> DescribeBody(const Registry& world, Entity entity)
		{
			BodyDesc desc;
			const glm::vec3 scale = world.Has<TransformComponent>(entity) ? glm::abs(world.Get<TransformComponent>(entity).Scale) : glm::vec3(1.0f);

			// One collider an entity: the box over the sphere, the sphere over the capsule
			if (world.Has<BoxColliderComponent>(entity))
			{
				desc.Shape = BodyShape{ .Kind = EShapeKind::Box, .HalfExtents = world.Get<BoxColliderComponent>(entity).HalfExtents * scale };
			}
			else if (world.Has<SphereColliderComponent>(entity))
			{
				desc.Shape = BodyShape{ .Kind = EShapeKind::Sphere,
					.Radius = world.Get<SphereColliderComponent>(entity).Radius * std::max({ scale.x, scale.y, scale.z }) };
			}
			else if (world.Has<CapsuleColliderComponent>(entity))
			{
				const CapsuleColliderComponent& capsule = world.Get<CapsuleColliderComponent>(entity);
				desc.Shape = BodyShape{ .Kind = EShapeKind::Capsule,
					.Radius = capsule.Radius * std::max(scale.x, scale.z),
					.HalfHeight = capsule.HalfHeight * scale.y };
			}
			else
			{
				return std::nullopt;
			}

			if (world.Has<TransformComponent>(entity))
			{
				const TransformComponent& transform = world.Get<TransformComponent>(entity);
				// As set, not normalized: compared with what the bodies wrote back, which must match
				// exactly (PoseMoved()); PhysicsWorld normalizes rotations itself
				desc.Position = transform.Position;
				desc.Rotation = transform.Rotation;
			}

			desc.Motion = EBodyMotion::Static;
			if (world.Has<RigidBodyComponent>(entity))
			{
				const RigidBodyComponent& rigidBody = world.Get<RigidBodyComponent>(entity);
				desc.Motion = rigidBody.bKinematic ? EBodyMotion::Kinematic : EBodyMotion::Dynamic;
				desc.Mass = rigidBody.Mass;
				desc.LinearDamping = rigidBody.LinearDamping;
				desc.AngularDamping = rigidBody.AngularDamping;
				desc.GravityFactor = rigidBody.GravityFactor;
			}

			if (world.Has<PhysicsMaterialComponent>(entity))
			{
				const PhysicsMaterialComponent& material = world.Get<PhysicsMaterialComponent>(entity);
				desc.Friction = material.Friction;
				desc.Restitution = material.Restitution;
			}

			desc.UserData = entity.Value;
			return desc;
		}

		// FNV-1a over everything of a description but its pose and user data
		uint64_t HashBody(const BodyDesc& desc)
		{
			uint64_t hash = 14695981039346656037ull;
			const auto add = [&hash](const auto& value)
			{
				unsigned char bytes[sizeof(value)];
				std::memcpy(bytes, &value, sizeof(value));
				for (const unsigned char byte : bytes)
				{
					hash = (hash ^ byte) * 1099511628211ull;
				}
			};
			add(desc.Shape.Kind);
			add(desc.Shape.HalfExtents.x);
			add(desc.Shape.HalfExtents.y);
			add(desc.Shape.HalfExtents.z);
			add(desc.Shape.Radius);
			add(desc.Shape.HalfHeight);
			add(desc.Motion);
			add(desc.Mass);
			add(desc.Friction);
			add(desc.Restitution);
			add(desc.LinearDamping);
			add(desc.AngularDamping);
			add(desc.GravityFactor);
			return hash;
		}

		// Whether a transform was set from outside since the body was last put or left there.
		// Transforms the bodies wrote themselves match exactly.
		bool PoseMoved(const BodyPose& last, const BodyPose& now)
		{
			return last.Position != now.Position || last.Rotation != now.Rotation;
		}

		void ForEachColliderEntity(Registry& world, const auto& visit)
		{
			world.Each<BoxColliderComponent>([&](Entity entity, BoxColliderComponent&)
				{
					visit(entity);
				});
			world.Each<SphereColliderComponent>([&](Entity entity, SphereColliderComponent&)
				{
					visit(entity);
				});
			world.Each<CapsuleColliderComponent>([&](Entity entity, CapsuleColliderComponent&)
				{
					visit(entity);
				});
		}
	}

	void PhysicsBodies::SyncBeforeStep(Registry& world, PhysicsWorld& physics, float seconds)
	{
		NYX_PROFILE_SCOPE("PhysicsBodies::SyncBeforeStep");
		for (auto& [key, tracked] : Bodies)
		{
			tracked.bSeen = false;
		}

		ForEachColliderEntity(world, [&](Entity entity)
			{
				auto found = Bodies.find(entity.Value);
				// An entity with two colliders is visited twice; its first visit decided
				if (found != Bodies.end() && found->second.bSeen)
				{
					return;
				}

				const std::optional<BodyDesc> desc = DescribeBody(world, entity);
				const uint64_t hash = HashBody(*desc);
				const BodyPose pose{ desc->Position, desc->Rotation };

				if (found != Bodies.end() && found->second.DescriptionHash != hash)
				{
					// Its shape, scale, motion or values changed: made again where its transform is
					physics.DestroyBody(found->second.Body);
					Bodies.erase(found);
					found = Bodies.end();
				}

				if (found == Bodies.end())
				{
					const BodyHandle body = physics.CreateBody(*desc);
					if (!body.IsValid())
					{
						return;
					}
					Bodies.emplace(entity.Value,
						TrackedBody{ .Owner = entity,
							.Body = body,
							.DescriptionHash = hash,
							.bKinematic = desc->Motion == EBodyMotion::Kinematic,
							.bDynamic = desc->Motion == EBodyMotion::Dynamic,
							.LastPose = pose,
							.bSeen = true });
					return;
				}

				TrackedBody& tracked = found->second;
				tracked.bSeen = true;
				if (tracked.bKinematic)
				{
					// Follows its transform over the step, pushing what it meets
					physics.MoveKinematic(tracked.Body, pose, seconds);
					tracked.LastPose = pose;
				}
				else if (PoseMoved(tracked.LastPose, pose))
				{
					physics.Teleport(tracked.Body, pose);
					tracked.LastPose = pose;
				}
			});

		WarnOfRigidBodiesWithoutCollider(world);

		// Bodies whose collider or entity is gone
		for (auto it = Bodies.begin(); it != Bodies.end();)
		{
			if (!it->second.bSeen)
			{
				physics.DestroyBody(it->second.Body);
				it = Bodies.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PhysicsBodies::WarnOfRigidBodiesWithoutCollider(const Registry& world)
	{
		world.Each<RigidBodyComponent>([&](Entity entity, const RigidBodyComponent&)
			{
				const bool bHasCollider = world.Has<BoxColliderComponent>(entity) || world.Has<SphereColliderComponent>(entity) ||
					world.Has<CapsuleColliderComponent>(entity);
				if (bHasCollider)
				{
					WarnedWithoutCollider.erase(entity.Value);
					return;
				}
				if (!WarnedWithoutCollider.insert(entity.Value).second)
				{
					return;
				}

				// By its name, else its guid, as the editor shows them
				std::string label = "An entity";
				if (world.Has<NameComponent>(entity) && !world.Get<NameComponent>(entity).Name.empty())
				{
					label = "'" + world.Get<NameComponent>(entity).Name + "'";
				}
				else if (world.Has<GuidComponent>(entity))
				{
					label = "Entity " + std::to_string(world.Get<GuidComponent>(entity).Guid.Value);
				}
				LOG_WARNING("{0} has a Rigid Body but no collider, so it has no body and doesn't move: add a Box, "
							"Sphere or Capsule Collider",
					label);
			});
	}

	void PhysicsBodies::WriteBackAfterStep(Registry& world, PhysicsWorld& physics)
	{
		NYX_PROFILE_SCOPE("PhysicsBodies::WriteBackAfterStep");
		for (auto& [key, tracked] : Bodies)
		{
			if (!tracked.bDynamic || !world.IsAlive(tracked.Owner) || !world.Has<TransformComponent>(tracked.Owner))
			{
				continue;
			}

			const BodyPose pose = physics.GetPose(tracked.Body);
			TransformComponent& transform = world.Get<TransformComponent>(tracked.Owner);
			transform.Position = pose.Position;
			transform.Rotation = pose.Rotation;
			tracked.LastPose = pose;
		}
	}

	BodyHandle PhysicsBodies::FindBody(Entity entity) const
	{
		const auto found = Bodies.find(entity.Value);
		return found != Bodies.end() ? found->second.Body : BodyHandle{};
	}

	std::optional<Entity> PhysicsBodies::FindEntity(const PhysicsWorld& physics, BodyHandle body) const
	{
		if (!body.IsValid())
		{
			return std::nullopt;
		}
		const auto found = Bodies.find(physics.GetUserData(body));
		if (found == Bodies.end() || found->second.Body != body)
		{
			return std::nullopt;
		}
		return found->second.Owner;
	}
}

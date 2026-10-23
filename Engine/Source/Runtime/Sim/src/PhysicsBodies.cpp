#include "NyxPCH.h"
#include "PhysicsBodies.h"

#include "CharacterComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "PhysicsComponents.h"
#include "PreviousTransformComponent.h"
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

		// A character's capsule is its shape, so its entity's colliders make no body
		void ForEachColliderEntity(Registry& world, const auto& visit)
		{
			const auto visitUnlessCharacter = [&](Entity entity)
			{
				if (!world.Has<CharacterComponent>(entity))
				{
					visit(entity);
				}
			};
			world.Each<BoxColliderComponent>([&](Entity entity, BoxColliderComponent&)
				{
					visitUnlessCharacter(entity);
				});
			world.Each<SphereColliderComponent>([&](Entity entity, SphereColliderComponent&)
				{
					visitUnlessCharacter(entity);
				});
			world.Each<CapsuleColliderComponent>([&](Entity entity, CapsuleColliderComponent&)
				{
					visitUnlessCharacter(entity);
				});
		}

		CharacterDesc DescribeCharacter(const CharacterComponent& character, const TransformComponent& transform, Entity entity)
		{
			return CharacterDesc{
				.Radius = character.Radius,
				.Height = character.Height,
				.Position = transform.Position,
				.MaxSlope = glm::radians(character.MaxSlopeDegrees),
				.UserData = entity.Value,
			};
		}

		// FNV-1a over what makes a character's shape; its movement values count from the next step
		// without making it again
		uint64_t HashCharacter(const CharacterDesc& desc)
		{
			uint64_t hash = 14695981039346656037ull;
			for (const float value : { desc.Radius, desc.Height, desc.MaxSlope, desc.Mass, desc.MaxPushForce })
			{
				unsigned char bytes[sizeof(value)];
				std::memcpy(bytes, &value, sizeof(value));
				for (const unsigned char byte : bytes)
				{
					hash = (hash ^ byte) * 1099511628211ull;
				}
			}
			return hash;
		}

		CharacterMove MakeCharacterMove(const CharacterComponent& character)
		{
			// Along the ground, and no faster than the walk speed
			glm::vec3 move(character.DesiredMove.x, 0.0f, character.DesiredMove.z);
			const float length = glm::length(move);
			if (length > 1.0f)
			{
				move /= length;
			}
			return CharacterMove{
				.WalkVelocity = move * character.WalkSpeed,
				.GroundAcceleration = character.Acceleration,
				.AirAcceleration = character.Acceleration * std::clamp(character.AirControl, 0.0f, 1.0f),
				.JumpSpeed = character.bJumpRequested ? character.JumpSpeed : 0.0f,
				.GravityFactor = character.GravityFactor,
				.StepHeight = character.StepHeight,
				.GroundSnapDistance = character.GroundSnapDistance,
			};
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
					// Drawn where it was put, not smeared from where it was
					world.Remove<PreviousTransformComponent>(entity);
				}
			});

		WarnOfRigidBodiesWithoutCollider(world);
		SyncCharacters(world, physics);

		// Bodies whose collider or entity is gone
		for (auto it = Bodies.begin(); it != Bodies.end();)
		{
			if (!it->second.bSeen)
			{
				world.Remove<PreviousTransformComponent>(it->second.Owner);
				physics.DestroyBody(it->second.Body);
				it = Bodies.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PhysicsBodies::SyncCharacters(Registry& world, PhysicsWorld& physics)
	{
		for (auto& [key, tracked] : Characters)
		{
			tracked.bSeen = false;
		}

		world.Each<CharacterComponent>([&](Entity entity, CharacterComponent& character)
			{
				if (!world.Has<TransformComponent>(entity))
				{
					return;
				}
				TransformComponent& transform = world.Get<TransformComponent>(entity);
				const CharacterDesc desc = DescribeCharacter(character, transform, entity);
				const uint64_t hash = HashCharacter(desc);

				auto found = Characters.find(entity.Value);
				if (found != Characters.end() && found->second.DescriptionHash != hash)
				{
					// Its shape changed: made again where it stands, from rest
					physics.DestroyCharacter(found->second.Character);
					Characters.erase(found);
					found = Characters.end();
				}

				if (found == Characters.end())
				{
					const CharacterHandle made = physics.CreateCharacter(desc);
					if (!made.IsValid())
					{
						return;
					}
					Characters.emplace(entity.Value,
						TrackedCharacter{ .Owner = entity,
							.Character = made,
							.Body = physics.GetCharacterBody(made),
							.DescriptionHash = hash,
							.LastPosition = transform.Position,
							.LastRotation = transform.Rotation,
							.bSeen = true });
					world.Remove<PreviousTransformComponent>(entity);
					return;
				}

				TrackedCharacter& tracked = found->second;
				tracked.bSeen = true;
				if (transform.Position != tracked.LastPosition || transform.Rotation != tracked.LastRotation)
				{
					// Set from outside since the last step, e.g. a live edit: put there, and drawn
					// there rather than smeared from where it was
					physics.TeleportCharacter(tracked.Character, transform.Position);
					tracked.LastPosition = transform.Position;
					tracked.LastRotation = transform.Rotation;
					world.Remove<PreviousTransformComponent>(entity);
				}
			});

		// Characters whose component, transform or entity is gone
		for (auto it = Characters.begin(); it != Characters.end();)
		{
			if (!it->second.bSeen)
			{
				world.Remove<PreviousTransformComponent>(it->second.Owner);
				physics.DestroyCharacter(it->second.Character);
				it = Characters.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PhysicsBodies::MoveCharacters(Registry& world, PhysicsWorld& physics, float seconds)
	{
		NYX_PROFILE_SCOPE("PhysicsBodies::MoveCharacters");
		for (auto& [key, tracked] : Characters)
		{
			// A system may have destroyed it, or taken its parts, since SyncBeforeStep()
			if (!world.IsAlive(tracked.Owner) || !world.Has<CharacterComponent>(tracked.Owner) || !world.Has<TransformComponent>(tracked.Owner))
			{
				continue;
			}
			CharacterComponent& character = world.Get<CharacterComponent>(tracked.Owner);
			TransformComponent& transform = world.Get<TransformComponent>(tracked.Owner);

			// A system that put it somewhere, e.g. back at the start, teleports it as a live edit
			// does; one that turned it, e.g. to face where it walks, is drawn turning
			BodyPose before{ tracked.LastPosition, tracked.LastRotation };
			if (transform.Position != tracked.LastPosition)
			{
				physics.TeleportCharacter(tracked.Character, transform.Position);
				before.Position = transform.Position;
			}

			physics.MoveCharacter(tracked.Character, MakeCharacterMove(character), seconds);
			character.bJumpRequested = false;

			const CharacterState state = physics.GetCharacterState(tracked.Character);
			transform.Position = state.Position;
			character.bOnGround = state.Ground == ECharacterGround::OnGround;
			character.GroundNormal = state.GroundNormal;
			character.Velocity = state.Velocity;
			tracked.LastPosition = transform.Position;
			tracked.LastRotation = transform.Rotation;

			// Drawn between where it was and where it got to, as the dynamic bodies are
			if (PoseMoved(before, BodyPose{ transform.Position, transform.Rotation }))
			{
				PreviousTransformComponent& previous = world.Has<PreviousTransformComponent>(tracked.Owner)
					? world.Get<PreviousTransformComponent>(tracked.Owner)
					: world.Add<PreviousTransformComponent>(tracked.Owner);
				SetPreviousTransform(previous, before.Position, before.Rotation, transform);
			}
			else
			{
				world.Remove<PreviousTransformComponent>(tracked.Owner);
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
			const bool bMoving = tracked.bDynamic || tracked.bKinematic;
			if (!bMoving || !world.IsAlive(tracked.Owner) || !world.Has<TransformComponent>(tracked.Owner))
			{
				// Not moved by the step, e.g. made static since: drawn where its transform is
				world.Remove<PreviousTransformComponent>(tracked.Owner);
				continue;
			}

			TransformComponent& transform = world.Get<TransformComponent>(tracked.Owner);
			BodyPose before{ transform.Position, transform.Rotation };
			if (tracked.bDynamic)
			{
				const BodyPose pose = physics.GetPose(tracked.Body);
				transform.Position = pose.Position;
				transform.Rotation = pose.Rotation;
				tracked.LastPose = pose;
			}
			else
			{
				// A kinematic body goes where its transform is: from where that was as the step
				// began to where the step's systems put it. One set between steps, by a live edit,
				// starts from there, as a teleported body does.
				before = tracked.LastPose;
			}

			// Where it was before the step, to draw it between the two; a body that didn't move has
			// none, so still scenes cost nothing
			if (PoseMoved(before, BodyPose{ transform.Position, transform.Rotation }))
			{
				PreviousTransformComponent& previous = world.Has<PreviousTransformComponent>(tracked.Owner)
					? world.Get<PreviousTransformComponent>(tracked.Owner)
					: world.Add<PreviousTransformComponent>(tracked.Owner);
				SetPreviousTransform(previous, before.Position, before.Rotation, transform);
			}
			else
			{
				world.Remove<PreviousTransformComponent>(tracked.Owner);
			}
		}
	}

	BodyHandle PhysicsBodies::FindBody(Entity entity) const
	{
		const auto found = Bodies.find(entity.Value);
		if (found != Bodies.end())
		{
			return found->second.Body;
		}
		const auto character = Characters.find(entity.Value);
		return character != Characters.end() ? character->second.Body : BodyHandle{};
	}

	std::optional<Entity> PhysicsBodies::FindEntity(const PhysicsWorld& physics, BodyHandle body) const
	{
		if (!body.IsValid())
		{
			return std::nullopt;
		}
		const uint64_t owner = physics.GetUserData(body);
		const auto found = Bodies.find(owner);
		if (found != Bodies.end() && found->second.Body == body)
		{
			return found->second.Owner;
		}
		const auto character = Characters.find(owner);
		if (character != Characters.end() && character->second.Body == body)
		{
			return character->second.Owner;
		}
		return std::nullopt;
	}

	CharacterHandle PhysicsBodies::FindCharacter(Entity entity) const
	{
		const auto found = Characters.find(entity.Value);
		return found != Characters.end() ? found->second.Character : CharacterHandle{};
	}
}

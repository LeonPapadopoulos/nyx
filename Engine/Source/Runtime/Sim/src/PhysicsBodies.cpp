#include "NyxPCH.h"
#include "PhysicsBodies.h"

#include "CharacterComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "PhysicsComponents.h"
#include "PhysicsMaterials.h"
#include "PreviousTransformComponent.h"
#include "Profiler.h"
#include "RuntimeMeshComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nyx
{
	namespace
	{
		// What a body is made from: its description, or nothing for an entity without a collider. A
		// mesh's triangles aren't in it (MakeTriangleMesh()): they are made only when the body is.
		std::optional<BodyDesc> DescribeBody(const Registry& world, PhysicsMaterials& materials, Entity entity)
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
			else if (world.Has<MeshColliderComponent>(entity))
			{
				desc.Shape = BodyShape{ .Kind = EShapeKind::Mesh };
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
			if (world.Has<RigidBodyComponent>(entity) && desc.Shape.Kind != EShapeKind::Mesh)
			{
				const RigidBodyComponent& rigidBody = world.Get<RigidBodyComponent>(entity);
				desc.Motion = rigidBody.bKinematic ? EBodyMotion::Kinematic : EBodyMotion::Dynamic;
				desc.Mass = rigidBody.Mass;
				desc.LinearDamping = rigidBody.LinearDamping;
				desc.AngularDamping = rigidBody.AngularDamping;
				desc.GravityFactor = rigidBody.GravityFactor;
			}

			// The collider's physics material, its surface over one entity's own
			const PhysicsMaterialData& material = GetPhysicsMaterial(world, materials, entity);
			desc.Friction = material.Friction;
			desc.Restitution = material.Restitution;
			desc.Density = material.Density;
			if (world.Has<PhysicsMaterialComponent>(entity))
			{
				const PhysicsMaterialComponent& surface = world.Get<PhysicsMaterialComponent>(entity);
				desc.Friction = surface.Friction;
				desc.Restitution = surface.Restitution;
			}

			desc.UserData = entity.Value;
			return desc;
		}

		// FNV-1a over everything of a description but its pose, its user data and its surface, which
		// reaches the body without making it again; the density only where it gives the mass
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
			add(desc.Motion == EBodyMotion::Dynamic && desc.Mass <= 0.0f ? desc.Density : 0.0f);
			add(desc.LinearDamping);
			add(desc.AngularDamping);
			add(desc.GravityFactor);
			return hash;
		}

		// What a mesh collider's triangles are made from, added to its description's hash: its mesh's
		// data and revision, the entity's scale, signed as it mirrors the mesh, and the smooth edges'
		// angle. Its address only tells data apart: the hash is never kept beyond the world.
		uint64_t HashMeshSource(uint64_t hash, const Registry& world, Entity entity)
		{
			const auto add = [&hash](const auto& value)
			{
				unsigned char bytes[sizeof(value)];
				std::memcpy(bytes, &value, sizeof(value));
				for (const unsigned char byte : bytes)
				{
					hash = (hash ^ byte) * 1099511628211ull;
				}
			};
			const RuntimeMeshComponent* runtimeMesh = world.Has<RuntimeMeshComponent>(entity) ? &world.Get<RuntimeMeshComponent>(entity) : nullptr;
			add(runtimeMesh ? reinterpret_cast<uintptr_t>(runtimeMesh->Mesh.get()) : uintptr_t(0));
			add(runtimeMesh ? runtimeMesh->Revision : uint64_t(0));
			const glm::vec3 scale = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Scale : glm::vec3(1.0f);
			add(scale.x);
			add(scale.y);
			add(scale.z);
			add(world.Get<MeshColliderComponent>(entity).SmoothEdgeDegrees);
			return hash;
		}

		// The triangles of the entity's mesh made at run time, with its scale; none, logged, for a
		// mesh that isn't there, is empty or isn't valid
		std::shared_ptr<const TriangleMesh> MakeTriangleMesh(const Registry& world, Entity entity)
		{
			const RuntimeMeshComponent* runtimeMesh = world.Has<RuntimeMeshComponent>(entity) ? &world.Get<RuntimeMeshComponent>(entity) : nullptr;
			if (!runtimeMesh || !runtimeMesh->Mesh || runtimeMesh->Mesh->Indices.empty())
			{
				// Usually not made yet: its generator's system runs after the bodies are synced
				return nullptr;
			}
			const MeshData& mesh = *runtimeMesh->Mesh;
			if (const std::string problem = CheckMeshData(mesh); !problem.empty())
			{
				LOG_ERROR("A Mesh Collider's mesh isn't valid, so it has no body: {0}", problem);
				return nullptr;
			}

			const glm::vec3 scale = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Scale : glm::vec3(1.0f);
			auto triangles = std::make_shared<TriangleMesh>();
			triangles->Vertices.reserve(mesh.Vertices.size());
			for (const Vertex& vertex : mesh.Vertices)
			{
				triangles->Vertices.push_back(vertex.Position * scale);
			}
			triangles->Indices = mesh.Indices;
			// Mirrored by the scale, the triangles turn their backs to where they faced: turned round
			if (scale.x * scale.y * scale.z < 0.0f)
			{
				for (size_t index = 0; index + 2 < triangles->Indices.size(); index += 3)
				{
					std::swap(triangles->Indices[index + 1], triangles->Indices[index + 2]);
				}
			}
			triangles->SmoothEdgeAngle = glm::radians(world.Get<MeshColliderComponent>(entity).SmoothEdgeDegrees);
			return triangles;
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
			world.Each<MeshColliderComponent>([&](Entity entity, MeshColliderComponent&)
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

	void PhysicsBodies::SyncBeforeStep(Registry& world, PhysicsWorld& physics, PhysicsMaterials& materials, float seconds)
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

				std::optional<BodyDesc> desc = DescribeBody(world, materials, entity);
				const bool bMesh = desc->Shape.Kind == EShapeKind::Mesh;
				const uint64_t hash = bMesh ? HashMeshSource(HashBody(*desc), world, entity) : HashBody(*desc);
				const BodyPose pose{ desc->Position, desc->Rotation };

				// A mesh body is kept as it was while its mesh keeps changing
				if (found != Bodies.end() && found->second.DescriptionHash != hash && IsMeshBodyDue(found->second, bMesh, hash))
				{
					// Its shape, scale, motion or values changed: made again where its transform is
					physics.DestroyBody(found->second.Body);
					Bodies.erase(found);
					found = Bodies.end();
				}

				if (found == Bodies.end())
				{
					// A mesh without triangles yet, or not valid, is kept without a body until its
					// mesh changes, rather than tried again every step
					if (bMesh)
					{
						desc->Shape.Mesh = MakeTriangleMesh(world, entity);
					}
					const BodyHandle body = !bMesh || desc->Shape.Mesh ? physics.CreateBody(*desc) : BodyHandle{};
					if (!body.IsValid() && !bMesh)
					{
						return;
					}
					Bodies.emplace(entity.Value,
						TrackedBody{ .Owner = entity,
							.Body = body,
							.DescriptionHash = hash,
							.bKinematic = desc->Motion == EBodyMotion::Kinematic,
							.bDynamic = desc->Motion == EBodyMotion::Dynamic,
							.Friction = desc->Friction,
							.Restitution = desc->Restitution,
							.LastPose = pose,
							.bSeen = true,
							.bMesh = bMesh });
					return;
				}

				TrackedBody& tracked = found->second;
				tracked.bSeen = true;
				if (tracked.DescriptionHash == hash)
				{
					// Changed back before its body was made again, or never changed
					tracked.PendingHash = 0;
					tracked.PendingStillSteps = 0;
					tracked.StaleSteps = 0;
				}
				if (!tracked.Body.IsValid())
				{
					return;
				}
				// A surface changed, e.g. its material tuned live: the body keeps moving as it was
				if (tracked.Friction != desc->Friction)
				{
					physics.SetFriction(tracked.Body, desc->Friction);
					tracked.Friction = desc->Friction;
				}
				if (tracked.Restitution != desc->Restitution)
				{
					physics.SetRestitution(tracked.Body, desc->Restitution);
					tracked.Restitution = desc->Restitution;
				}
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

	// Whether a mesh body whose description is now `hash` is made again this step: at once if it
	// has no body or wasn't a mesh; else once the new description has held still for
	// MeshSettleSteps, or the body has been behind for MeshMostStaleSteps
	bool PhysicsBodies::IsMeshBodyDue(TrackedBody& tracked, bool bMesh, uint64_t hash)
	{
		if (!bMesh || !tracked.bMesh || !tracked.Body.IsValid())
		{
			return true;
		}
		if (tracked.PendingHash != hash)
		{
			tracked.PendingHash = hash;
			tracked.PendingStillSteps = 0;
		}
		else
		{
			++tracked.PendingStillSteps;
		}
		++tracked.StaleSteps;
		return tracked.PendingStillSteps >= MeshSettleSteps || tracked.StaleSteps >= MeshMostStaleSteps;
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
		// By its name, else its guid, as the editor shows them
		const auto labelOf = [&world](Entity entity)
		{
			if (world.Has<NameComponent>(entity) && !world.Get<NameComponent>(entity).Name.empty())
			{
				return "'" + world.Get<NameComponent>(entity).Name + "'";
			}
			if (world.Has<GuidComponent>(entity))
			{
				return "Entity " + std::to_string(world.Get<GuidComponent>(entity).Guid.Value);
			}
			return std::string("An entity");
		};

		world.Each<RigidBodyComponent>([&](Entity entity, const RigidBodyComponent&)
			{
				const bool bHasMovingCollider = world.Has<BoxColliderComponent>(entity) || world.Has<SphereColliderComponent>(entity) ||
					world.Has<CapsuleColliderComponent>(entity);
				const bool bHasMeshCollider = !bHasMovingCollider && world.Has<MeshColliderComponent>(entity);
				if (bHasMovingCollider || bHasMeshCollider)
				{
					WarnedWithoutCollider.erase(entity.Value);
				}
				if (!bHasMeshCollider)
				{
					WarnedStillMesh.erase(entity.Value);
				}

				if (bHasMeshCollider && WarnedStillMesh.insert(entity.Value).second)
				{
					LOG_WARNING("{0} has a Rigid Body and a Mesh Collider: a mesh's body is always static, so it doesn't move", labelOf(entity));
				}
				if (!bHasMovingCollider && !bHasMeshCollider && WarnedWithoutCollider.insert(entity.Value).second)
				{
					LOG_WARNING("{0} has a Rigid Body but no collider, so it has no body and doesn't move: add a Box, "
								"Sphere or Capsule Collider",
						labelOf(entity));
				}
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

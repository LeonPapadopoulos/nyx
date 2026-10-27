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

		// One more value in an FNV-1a hash
		template<typename T>
		void HashValue(uint64_t& hash, const T& value)
		{
			unsigned char bytes[sizeof(value)];
			std::memcpy(bytes, &value, sizeof(value));
			for (const unsigned char byte : bytes)
			{
				hash = (hash ^ byte) * 1099511628211ull;
			}
		}

		// What makes an entity's part of a group's body: its description and its surface
		uint64_t HashMember(Entity entity, const BodyDesc& desc)
		{
			uint64_t hash = HashBody(desc);
			HashValue(hash, entity.Value);
			HashValue(hash, desc.Friction);
			HashValue(hash, desc.Restitution);
			return hash;
		}

		// What a group's body is made from: its entities, in order, each one's part (HashMember())
		uint64_t HashGroup(const std::map<uint64_t, uint64_t>& memberHashes, const std::vector<Entity>& members)
		{
			uint64_t hash = 14695981039346656037ull;
			for (const Entity member : members)
			{
				const auto found = memberHashes.find(member.Value);
				HashValue(hash, found != memberHashes.end() ? found->second : 0ull);
			}
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

		// Groups first: what goes keeps its entities' velocities for the bodies made in its place
		std::map<uint64_t, KeptVelocity> kept;
		const std::map<uint64_t, std::vector<Entity>> newGroups = UnmakeChangedGroups(world, physics, materials, kept);
		std::set<uint64_t> joining;
		for (const auto& [group, members] : newGroups)
		{
			for (const Entity member : members)
			{
				joining.insert(member.Value);
			}
		}

		ForEachColliderEntity(world, [&](Entity entity)
			{
				// A part of a group's body
				if (GroupOf.contains(entity.Value) || joining.contains(entity.Value))
				{
					return;
				}
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
					// Out of a group's body: as its point of it moved
					if (const auto velocity = kept.find(entity.Value); velocity != kept.end())
					{
						desc->LinearVelocity = velocity->second.Linear;
						desc->AngularVelocity = velocity->second.Angular;
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

		MakeGroups(world, physics, materials, newGroups, kept);
	}

	std::map<uint64_t, std::vector<Entity>> PhysicsBodies::UnmakeChangedGroups(Registry& world, PhysicsWorld& physics, PhysicsMaterials& materials,
		std::map<uint64_t, KeptVelocity>& kept)
	{
		// The groups asked for, of two entities or more: dynamic rigid bodies' colliders, by group, in
		// the order of their entities
		// Each entity described once a step, as a body of its own would be
		std::map<uint64_t, std::vector<Entity>> wanted;
		MemberHashes.clear();
		world.Each<BodyGroupComponent>([&](Entity entity, BodyGroupComponent& group)
			{
				if (group.Group == 0 || world.Has<CharacterComponent>(entity))
				{
					return;
				}
				const std::optional<BodyDesc> desc = DescribeBody(world, materials, entity);
				if (desc && desc->Motion == EBodyMotion::Dynamic && desc->Shape.Kind != EShapeKind::Mesh)
				{
					wanted[group.Group].push_back(entity);
					MemberHashes[entity.Value] = HashMember(entity, *desc);
				}
			});
		for (auto it = wanted.begin(); it != wanted.end();)
		{
			std::sort(it->second.begin(), it->second.end(), [](Entity a, Entity b)
				{
					return a.Value < b.Value;
				});
			it = it->second.size() < 2 ? wanted.erase(it) : std::next(it);
		}

		// Groups whose entities or their descriptions changed, or one of whose entities was put
		// somewhere from outside, go; each entity keeps the velocity of its point of it, unless one was
		// put somewhere, which stops them all, as a teleported body stops
		for (auto it = Groups.begin(); it != Groups.end();)
		{
			TrackedGroup& group = it->second;
			const auto asked = wanted.find(it->first);
			bool bKeep = asked != wanted.end() && asked->second.size() == group.Members.size();
			for (size_t index = 0; bKeep && index < group.Members.size(); ++index)
			{
				bKeep = asked->second[index] == group.Members[index].Owner;
			}
			bKeep = bKeep && HashGroup(MemberHashes, asked->second) == group.DescriptionHash;
			bool bPutSomewhere = false;
			for (const GroupMember& member : group.Members)
			{
				if (world.IsAlive(member.Owner) && world.Has<TransformComponent>(member.Owner))
				{
					const TransformComponent& transform = world.Get<TransformComponent>(member.Owner);
					bPutSomewhere = bPutSomewhere || PoseMoved(member.LastPose, BodyPose{ transform.Position, transform.Rotation });
				}
			}
			if (bKeep && !bPutSomewhere)
			{
				++it;
				continue;
			}

			for (const GroupMember& member : group.Members)
			{
				if (!bPutSomewhere && world.IsAlive(member.Owner))
				{
					kept[member.Owner.Value] =
						KeptVelocity{ physics.GetPointVelocity(group.Body, member.LastPose.Position), physics.GetAngularVelocity(group.Body) };
				}
				GroupOf.erase(member.Owner.Value);
			}
			physics.DestroyBody(group.Body);
			it = Groups.erase(it);
		}

		// The groups to make: their entities' own bodies go, each keeping its velocity
		for (auto it = wanted.begin(); it != wanted.end();)
		{
			if (Groups.contains(it->first))
			{
				it = wanted.erase(it);
				continue;
			}
			for (const Entity member : it->second)
			{
				const auto own = Bodies.find(member.Value);
				if (own == Bodies.end())
				{
					continue;
				}
				if (own->second.Body.IsValid() && !kept.contains(member.Value) && world.Has<TransformComponent>(member))
				{
					const TransformComponent& transform = world.Get<TransformComponent>(member);
					const bool bPutSomewhere = PoseMoved(own->second.LastPose, BodyPose{ transform.Position, transform.Rotation });
					kept[member.Value] = bPutSomewhere ? KeptVelocity{}
													   : KeptVelocity{ physics.GetPointVelocity(own->second.Body, transform.Position),
															 physics.GetAngularVelocity(own->second.Body) };
				}
				physics.DestroyBody(own->second.Body);
				Bodies.erase(own);
			}
			++it;
		}
		return wanted;
	}

	void PhysicsBodies::MakeGroups(Registry& world, PhysicsWorld& physics, PhysicsMaterials& materials,
		const std::map<uint64_t, std::vector<Entity>>& groups, const std::map<uint64_t, KeptVelocity>& kept)
	{
		for (const auto& [number, members] : groups)
		{
			// The first entity's frame is the body's, and its motion's values, damping and gravity, the
			// body's; each entity a part where it is in that frame now
			BodyDesc desc = *DescribeBody(world, materials, members.front());
			const glm::quat inverse = glm::inverse(glm::normalize(desc.Rotation));
			TrackedGroup group;
			for (const Entity member : members)
			{
				const BodyDesc part = *DescribeBody(world, materials, member);
				const BodyPose pose{ inverse * (part.Position - desc.Position), glm::normalize(inverse * glm::normalize(part.Rotation)) };
				const auto velocity = kept.find(member.Value);
				const KeptVelocity motion = velocity != kept.end() ? velocity->second : KeptVelocity{};
				desc.Parts.push_back(BodyPart{ .Shape = part.Shape,
					.Pose = pose,
					.Mass = part.Mass,
					.Density = part.Density,
					.Friction = part.Friction,
					.Restitution = part.Restitution,
					.UserData = member.Value,
					.LinearVelocity = motion.Linear,
					.AngularVelocity = motion.Angular });
				group.Members.push_back(GroupMember{ .Owner = member, .Pose = pose, .LastPose = BodyPose{ part.Position, part.Rotation } });
			}

			group.Body = physics.CreateBody(desc);
			if (!group.Body.IsValid())
			{
				continue;
			}
			group.DescriptionHash = HashGroup(MemberHashes, members);
			for (uint32_t part = 0; part < members.size(); ++part)
			{
				GroupOf[members[part].Value] = GroupPlace{ .Group = number, .Part = part };
			}
			Groups.emplace(number, std::move(group));
		}
	}

	void PhysicsBodies::WriteBackGroups(Registry& world, PhysicsWorld& physics)
	{
		for (auto& [number, group] : Groups)
		{
			const BodyPose pose = physics.GetPose(group.Body);
			for (GroupMember& member : group.Members)
			{
				if (!world.IsAlive(member.Owner) || !world.Has<TransformComponent>(member.Owner))
				{
					continue;
				}
				TransformComponent& transform = world.Get<TransformComponent>(member.Owner);
				const BodyPose before{ transform.Position, transform.Rotation };
				transform.Position = pose.Position + pose.Rotation * member.Pose.Position;
				transform.Rotation = pose.Rotation * member.Pose.Rotation;
				member.LastPose = BodyPose{ transform.Position, transform.Rotation };

				// Drawn between where it was and where it is, as a body of its own is
				if (PoseMoved(before, member.LastPose))
				{
					PreviousTransformComponent& previous = world.Has<PreviousTransformComponent>(member.Owner)
						? world.Get<PreviousTransformComponent>(member.Owner)
						: world.Add<PreviousTransformComponent>(member.Owner);
					SetPreviousTransform(previous, before.Position, before.Rotation, transform);
				}
				else
				{
					world.Remove<PreviousTransformComponent>(member.Owner);
				}
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
		WriteBackGroups(world, physics);
	}

	BodyHandle PhysicsBodies::FindBody(Entity entity) const
	{
		if (const auto grouped = GroupOf.find(entity.Value); grouped != GroupOf.end())
		{
			return Groups.at(grouped->second.Group).Body;
		}
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
		// A group's body has its first entity's number
		const auto grouped = GroupOf.find(owner);
		if (grouped != GroupOf.end() && Groups.at(grouped->second.Group).Body == body)
		{
			return Groups.at(grouped->second.Group).Members.front().Owner;
		}
		return std::nullopt;
	}

	std::optional<Entity> PhysicsBodies::FindEntity(const PhysicsWorld& physics, const QueryHit& hit) const
	{
		const std::optional<Entity> owner = FindEntity(physics, hit.Body);
		const auto grouped = owner ? GroupOf.find(owner->Value) : GroupOf.end();
		if (grouped == GroupOf.end())
		{
			return owner;
		}
		const auto part = GroupOf.find(hit.PartUserData);
		return part != GroupOf.end() && part->second.Group == grouped->second.Group ? Groups.at(part->second.Group).Members[part->second.Part].Owner
																					: owner;
	}

	std::optional<BodyPartOf> PhysicsBodies::FindPart(Entity entity) const
	{
		const auto grouped = GroupOf.find(entity.Value);
		if (grouped == GroupOf.end())
		{
			return std::nullopt;
		}
		const TrackedGroup& group = Groups.at(grouped->second.Group);
		const uint32_t part = grouped->second.Part;
		return BodyPartOf{ .Body = group.Body, .Part = part, .Pose = group.Members[part].Pose };
	}

	std::vector<Entity> PhysicsBodies::GetGroupEntities(const PhysicsWorld& physics, BodyHandle body) const
	{
		// A group's body has its first entity's number
		std::vector<Entity> entities;
		const auto grouped = body.IsValid() ? GroupOf.find(physics.GetUserData(body)) : GroupOf.end();
		if (grouped != GroupOf.end() && Groups.at(grouped->second.Group).Body == body)
		{
			for (const GroupMember& member : Groups.at(grouped->second.Group).Members)
			{
				entities.push_back(member.Owner);
			}
		}
		return entities;
	}

	CharacterHandle PhysicsBodies::FindCharacter(Entity entity) const
	{
		const auto found = Characters.find(entity.Value);
		return found != Characters.end() ? found->second.Character : CharacterHandle{};
	}
}

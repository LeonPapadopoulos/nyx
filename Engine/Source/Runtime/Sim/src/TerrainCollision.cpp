#include "NyxPCH.h"
#include "TerrainCollision.h"

#include "CharacterComponent.h"
#include "JobSystem.h"
#include "PhysicsBodies.h"
#include "PhysicsComponents.h"
#include "Profiler.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TransformComponent.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>

namespace Nyx
{
	namespace Detail
	{
		// A terrain's collision jobs, shared with them, so a job that finishes after its terrain or the
		// world went writes into this and nothing else
		struct TerrainCollisionJobs
		{
			struct Scheduled
			{
				JobHandle Job;
				uint64_t Shape = 0;
			};
			std::unordered_map<uint64_t, Scheduled> InFlight;

			struct Done
			{
				glm::ivec3 Key{ 0 };
				uint64_t Shape = 0;
				uint64_t Placement = 0;
				std::shared_ptr<const PreparedShape> Prepared;
				uint32_t Triangles = 0;
				bool bShared = false;
				std::chrono::steady_clock::time_point Asked;
			};
			std::mutex Lock;
			std::vector<Done> Finished;
		};
	}

	namespace
	{
		uint64_t PackKey(const glm::ivec3& key)
		{
			auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 20)) & 0x1FFFFFu); };
			return part(key.x) | (part(key.y) << 21) | (part(key.z) << 42);
		}

		glm::ivec3 UnpackKey(uint64_t packed)
		{
			auto part = [&](int shift) { return int((packed >> shift) & 0x1FFFFFu) - (1 << 20); };
			return { part(0), part(21), part(42) };
		}

		uint64_t HashPlacement(const TransformComponent& transform)
		{
			uint64_t hash = 0xCBF29CE484222325ull;
			auto add = [&](const void* data, size_t size)
			{
				const auto* bytes = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					hash ^= bytes[i];
					hash *= 0x100000001B3ull;
				}
			};
			add(&transform.Position, sizeof(transform.Position));
			add(&transform.Rotation, sizeof(transform.Rotation));
			add(&transform.Scale, sizeof(transform.Scale));
			return hash;
		}

		// A moving body as the collision sees it, in the terrain's space
		struct Mover
		{
			glm::vec3 Position{ 0.0f };
			glm::vec3 Velocity{ 0.0f };
			// Asleep: only the chunks it rests in
			bool bAwake = true;
		};

		// The chunks of `level` whose boxes come within `radius` of `point`, each needed at `time` and
		// `secondsPerMetre` later for each metre it is away, unless it is needed sooner already
		void AddChunksNear(std::unordered_map<uint64_t, float>& wanted, const glm::vec3& point, float radius, float time, int level, float secondsPerMetre = 0.0f)
		{
			const float size = GetTerrainChunkSize(level);
			const glm::ivec3 low(glm::floor((point - radius) / size));
			const glm::ivec3 high(glm::floor((point + radius) / size));
			for (int z = low.z; z <= high.z; ++z)
			{
				for (int y = low.y; y <= high.y; ++y)
				{
					for (int x = low.x; x <= high.x; ++x)
					{
						const glm::vec3 corner = glm::vec3(x, y, z) * size;
						const float distance = glm::length(point - glm::clamp(point, corner, corner + size));
						if (distance > radius)
						{
							continue;
						}
						const float when = time + distance * secondsPerMetre;
						const auto [it, bAdded] = wanted.emplace(PackKey(glm::ivec3(x, y, z)), when);
						if (!bAdded)
						{
							it->second = std::min(it->second, when);
						}
					}
				}
			}
		}

		// Where a mover will be at `time`, by its velocity and gravity
		glm::vec3 PathAt(const Mover& mover, const glm::vec3& gravity, float time)
		{
			return mover.Position + mover.Velocity * time + gravity * (0.5f * time * time);
		}

		// The chunks of the way ahead: points along it close enough that none is skipped between
		void AddChunksAhead(std::unordered_map<uint64_t, float>& wanted, const Mover& mover, const glm::vec3& gravity, int level)
		{
			const float size = GetTerrainChunkSize(level);
			const float fastest = glm::length(mover.Velocity) + glm::length(gravity) * TerrainCollision::LookaheadSeconds;
			const float step = std::clamp(0.25f * size / std::max(fastest, 0.001f), 0.02f, 0.25f);
			for (float time = step; time <= TerrainCollision::LookaheadSeconds; time += step)
			{
				AddChunksNear(wanted, PathAt(mover, gravity, time), TerrainCollision::LookaheadMargin, time, level);
			}
		}
	}

	TerrainCollision::TerrainCollision(JobSystem* jobs) : Workers(jobs != nullptr ? jobs : &JobSystem::Get())
	{
	}

	TerrainCollision::~TerrainCollision()
	{
		for (TerrainState& state : Terrains)
		{
			for (auto& [key, job] : state.Jobs->InFlight)
			{
				Workers->Cancel(job.Job);
			}
		}
	}

	std::optional<Entity> TerrainCollision::FindEntity(BodyHandle body) const
	{
		const auto found = BodyOwners.find(body.Value);
		return found != BodyOwners.end() ? std::optional<Entity>(found->second) : std::nullopt;
	}

	size_t TerrainCollision::GetBodyCount() const
	{
		return BodyOwners.size();
	}

	void TerrainCollision::Release(PhysicsWorld& physics, TerrainState& state)
	{
		for (auto& [key, chunk] : state.Chunks)
		{
			if (chunk.Body.IsValid())
			{
				BodyOwners.erase(chunk.Body.Value);
				physics.DestroyBody(chunk.Body);
			}
		}
		state.Chunks.clear();
		for (auto& [key, job] : state.Jobs->InFlight)
		{
			Workers->Cancel(job.Job);
		}
		state.Jobs->InFlight.clear();
	}

	void TerrainCollision::SyncBeforeStep(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies)
	{
		NYX_PROFILE_FUNCTION();
		for (TerrainState& state : Terrains)
		{
			state.bSeen = false;
		}
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent& terrain)
			{
				if (terrain.CollisionDistance > 0.0f)
				{
					terrains.push_back(entity);
				}
			});
		for (const Entity entity : terrains)
		{
			auto found = std::find_if(Terrains.begin(), Terrains.end(), [&](const TerrainState& state) { return state.Owner == entity; });
			if (found == Terrains.end())
			{
				TerrainState state;
				state.Owner = entity;
				state.Jobs = std::make_shared<Detail::TerrainCollisionJobs>();
				Terrains.push_back(std::move(state));
				found = Terrains.end() - 1;
			}
			found->bSeen = true;
			SyncTerrain(world, physics, bodies, *found);
		}

		// Terrains gone, or without collision now: their bodies go
		for (auto it = Terrains.begin(); it != Terrains.end();)
		{
			if (it->bSeen)
			{
				++it;
				continue;
			}
			Release(physics, *it);
			if (world.IsAlive(it->Owner) && world.Has<TerrainCollisionComponent>(it->Owner))
			{
				world.Remove<TerrainCollisionComponent>(it->Owner);
			}
			it = Terrains.erase(it);
		}
	}

	void TerrainCollision::SyncTerrain(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies, TerrainState& state)
	{
		const SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(state.Owner);
		const int level = std::clamp(terrain.FinestLevel, TerrainFinestLevel, TerrainCoarsestLevel);
		const float size = GetTerrainChunkSize(level);
		Detail::TerrainCollisionJobs& jobs = *state.Jobs;

		// Moved: every body made again where it is now
		const TransformComponent transform = world.Has<TransformComponent>(state.Owner) ? world.Get<TransformComponent>(state.Owner) : TransformComponent{};
		const uint64_t placement = HashPlacement(transform);
		if (placement != state.Placement)
		{
			Release(physics, state);
			state.Placement = placement;
		}
		// New values: made again, the old bodies kept until each one's new body is in
		const uint64_t shape = HashSdfTerrainShape(terrain);
		if (shape != state.Shape || !state.Snapshot)
		{
			state.Shape = shape;
			state.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
			state.Surface.clear();
			for (auto& [key, job] : jobs.InFlight)
			{
				Workers->Cancel(job.Job);
			}
			jobs.InFlight.clear();
		}

		// The moving bodies, in the terrain's space
		const glm::mat4 toWorld = transform.ToMatrix();
		const glm::mat4 toTerrain = glm::inverse(toWorld);
		const glm::mat3 turnToTerrain = glm::mat3(toTerrain);
		const glm::vec3 gravity = turnToTerrain * physics.GetGravity();
		std::vector<Mover> movers;
		world.Each<CharacterComponent>([&](Entity entity, CharacterComponent&)
			{
				const CharacterHandle character = bodies.FindCharacter(entity);
				if (character.IsValid())
				{
					const CharacterState moving = physics.GetCharacterState(character);
					movers.push_back({ glm::vec3(toTerrain * glm::vec4(moving.Position, 1.0f)), turnToTerrain * moving.Velocity, true });
				}
			});
		world.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent& rigidBody)
			{
				const BodyHandle body = bodies.FindBody(entity);
				if (rigidBody.bKinematic || !body.IsValid())
				{
					return;
				}
				movers.push_back({ glm::vec3(toTerrain * glm::vec4(physics.GetPose(body).Position, 1.0f)), turnToTerrain * physics.GetLinearVelocity(body),
					physics.IsAwake(body) });
			});

		// The chunks wanted, by when they are needed: round each mover now, then along its way
		std::unordered_map<uint64_t, float> wanted;
		std::unordered_map<uint64_t, float> kept;
		const float reach = std::max(terrain.CollisionDistance, 0.0f);
		for (const Mover& mover : movers)
		{
			// Round it, sooner the nearer, as soon as it could get there at its speed (walking's at
			// least), so a fast body's way ahead comes before what it passes by
			const float secondsPerMetre = 1.0f / std::max(glm::length(mover.Velocity), 2.0f);
			AddChunksNear(wanted, mover.Position, mover.bAwake ? reach : LookaheadMargin, 0.0f, level, secondsPerMetre);
			AddChunksNear(kept, mover.Position, (mover.bAwake ? reach : LookaheadMargin) + size, 0.0f, level);
			// Hardly moving: resting on something, which gravity alone would put the way ahead into
			if (mover.bAwake && glm::length(mover.Velocity) > MovingSpeed)
			{
				AddChunksAhead(wanted, mover, gravity, level);
			}
		}
		// Of them, those the surface crosses
		auto hasSurface = [&](uint64_t packed)
		{
			const auto found = state.Surface.find(packed);
			if (found != state.Surface.end())
			{
				return found->second;
			}
			const float cell = GetTerrainCellSize(level);
			const glm::vec3 corner = glm::vec3(UnpackKey(packed)) * size;
			const TerrainValueRange range = GetTerrainValueRange(terrain, corner - 2.0f * cell, corner + size + cell);
			const bool bSurface = !range.IsAllAir() && !range.IsAllSolid();
			state.Surface.emplace(packed, bSurface);
			return bSurface;
		};
		std::vector<std::pair<float, uint64_t>> order;
		uint32_t skipped = 0;
		for (const auto& [packed, time] : wanted)
		{
			if (hasSurface(packed))
			{
				order.push_back({ time, packed });
			}
			else
			{
				++skipped;
			}
		}
		std::sort(order.begin(), order.end());

		// What the workers made: bodies in place of those they replace, a few a step
		std::vector<Detail::TerrainCollisionJobs::Done> finished;
		{
			std::lock_guard lock(jobs.Lock);
			finished.swap(jobs.Finished);
		}
		const auto now = std::chrono::steady_clock::now();
		state.WorstSeconds *= 0.995;
		uint32_t made = 0;
		std::vector<Detail::TerrainCollisionJobs::Done> later;
		for (Detail::TerrainCollisionJobs::Done& done : finished)
		{
			const uint64_t packed = PackKey(done.Key);
			if (done.Shape != state.Shape || done.Placement != state.Placement)
			{
				continue;
			}
			if (made >= MaxBodiesPerStep)
			{
				later.push_back(std::move(done));
				continue;
			}
			jobs.InFlight.erase(packed);
			state.WorstSeconds = std::max(state.WorstSeconds, std::chrono::duration<double>(now - done.Asked).count());
			if (!wanted.contains(packed) && !kept.contains(packed))
			{
				continue;
			}
			ChunkBody chunk;
			chunk.Shape = done.Shape;
			chunk.Triangles = done.Triangles;
			if (done.Prepared)
			{
				BodyDesc desc;
				desc.Prepared = done.Prepared;
				desc.Motion = EBodyMotion::Static;
				desc.Position = glm::vec3(toWorld * glm::vec4(glm::vec3(done.Key) * size, 1.0f));
				desc.Rotation = transform.Rotation;
				desc.UserData = state.Owner.Value;
				chunk.Body = physics.CreateBody(desc);
			}
			const auto old = state.Chunks.find(packed);
			if (old != state.Chunks.end() && old->second.Body.IsValid())
			{
				BodyOwners.erase(old->second.Body.Value);
				physics.DestroyBody(old->second.Body);
			}
			if (chunk.Body.IsValid())
			{
				BodyOwners.emplace(chunk.Body.Value, state.Owner);
			}
			state.Chunks[packed] = chunk;
			++state.Made;
			state.Shared += done.bShared ? 1u : 0u;
			++made;
		}
		if (!later.empty())
		{
			std::lock_guard lock(jobs.Lock);
			jobs.Finished.insert(jobs.Finished.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
		}

		// Chunks a chunk farther than wanted go, and their jobs if they haven't started
		for (auto it = state.Chunks.begin(); it != state.Chunks.end();)
		{
			if (wanted.contains(it->first) || kept.contains(it->first))
			{
				++it;
				continue;
			}
			if (it->second.Body.IsValid())
			{
				BodyOwners.erase(it->second.Body.Value);
				physics.DestroyBody(it->second.Body);
			}
			it = state.Chunks.erase(it);
		}
		for (auto it = jobs.InFlight.begin(); it != jobs.InFlight.end();)
		{
			if (!wanted.contains(it->first) && !kept.contains(it->first) && Workers->Cancel(it->second.Job))
			{
				it = jobs.InFlight.erase(it);
				continue;
			}
			++it;
		}

		// The soonest wanted chunks without a body of the values now to the workers, from the chunk
		// drawn where the terrain's chunks have it at this level with no other level round it
		std::unordered_map<uint64_t, std::shared_ptr<const MeshData>> drawn;
		if (world.Has<TerrainChunksComponent>(state.Owner))
		{
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(state.Owner).Chunks)
			{
				if (chunk.Level == level && chunk.Shape == state.Shape && chunk.Neighbourhood == TerrainNeighbourhood{} && chunk.Mesh.Mesh)
				{
					drawn.emplace(PackKey(chunk.Key), chunk.Mesh.Mesh);
				}
			}
		}
		const uint32_t maxJobs = std::max(Workers->GetWorkerCount(), 1u);
		for (const auto& [time, packed] : order)
		{
			const auto resident = state.Chunks.find(packed);
			if ((resident != state.Chunks.end() && resident->second.Shape == state.Shape) || jobs.InFlight.contains(packed))
			{
				continue;
			}
			if (jobs.InFlight.size() >= maxJobs)
			{
				break;
			}
			std::shared_ptr<Detail::TerrainCollisionJobs> shared = state.Jobs;
			std::shared_ptr<const SdfTerrainComponent> snapshot = state.Snapshot;
			const auto found = drawn.find(packed);
			std::shared_ptr<const MeshData> mesh = found != drawn.end() ? found->second : nullptr;
			const glm::ivec3 key = UnpackKey(packed);
			const uint64_t jobShape = state.Shape;
			const uint64_t jobPlacement = state.Placement;
			const glm::vec3 scale = transform.Scale;
			const auto asked = std::chrono::steady_clock::now();
			JobHandle job = Workers->Schedule(
				[shared, snapshot, mesh, key, level, jobShape, jobPlacement, scale, asked]
				{
					Detail::TerrainCollisionJobs::Done done;
					done.Key = key;
					done.Shape = jobShape;
					done.Placement = jobPlacement;
					done.Asked = asked;
					done.bShared = mesh != nullptr;
					const MeshData made = mesh ? MeshData{} : MakeTerrainChunkMesh(*snapshot, { level, key });
					const MeshData& source = mesh ? *mesh : made;
					if (!source.Indices.empty())
					{
						auto triangles = std::make_shared<TriangleMesh>();
						triangles->Vertices.reserve(source.Vertices.size());
						for (const Vertex& vertex : source.Vertices)
						{
							triangles->Vertices.push_back(vertex.Position * scale);
						}
						triangles->Indices = source.Indices;
						done.Triangles = static_cast<uint32_t>(source.Indices.size() / 3);
						done.Prepared = PrepareBodyShape(BodyShape{ .Kind = EShapeKind::Mesh, .Mesh = std::move(triangles) });
					}
					std::lock_guard lock(shared->Lock);
					shared->Finished.push_back(std::move(done));
				},
				EJobPriority::Normal);
			jobs.InFlight.emplace(packed, Detail::TerrainCollisionJobs::Scheduled{ job, jobShape });
		}

		// What it did, and the bodies collision can't keep up with
		TerrainCollisionStats stats;
		stats.Wanted = static_cast<uint32_t>(order.size());
		stats.Skipped = skipped;
		stats.Pending = static_cast<uint32_t>(jobs.InFlight.size());
		stats.Made = state.Made;
		stats.Shared = state.Shared;
		stats.WorstSeconds = state.WorstSeconds;
		for (const auto& [packed, chunk] : state.Chunks)
		{
			stats.Resident += chunk.Shape == state.Shape ? 1u : 0u;
			stats.Triangles += chunk.Triangles;
		}
		auto isMissing = [&](uint64_t packed)
		{
			const auto resident = state.Chunks.find(packed);
			return hasSurface(packed) && resident == state.Chunks.end();
		};
		for (const Mover& mover : movers)
		{
			std::unordered_map<uint64_t, float> here;
			AddChunksNear(here, mover.Position, LookaheadMargin, 0.0f, level);
			bool bUncovered = false;
			for (const auto& [packed, time] : here)
			{
				bUncovered = bUncovered || isMissing(packed);
			}
			stats.Uncovered += bUncovered ? 1u : 0u;
			if (!bUncovered && mover.bAwake && glm::length(mover.Velocity) > MovingSpeed)
			{
				std::unordered_map<uint64_t, float> ahead;
				AddChunksAhead(ahead, mover, gravity, level);
				bool bOutrunning = false;
				for (const auto& [packed, time] : ahead)
				{
					bOutrunning = bOutrunning || (time < state.WorstSeconds && isMissing(packed));
				}
				stats.Outrunning += bOutrunning ? 1u : 0u;
			}
		}
		if (!world.Has<TerrainCollisionComponent>(state.Owner))
		{
			world.Add<TerrainCollisionComponent>(state.Owner, TerrainCollisionComponent{});
		}
		world.Get<TerrainCollisionComponent>(state.Owner).Stats = stats;
	}
}

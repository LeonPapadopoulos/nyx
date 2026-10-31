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
#include "TerrainWorldPlan.h"
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
				uint64_t Edits = 0;
			};
			std::unordered_map<uint64_t, Scheduled> InFlight;

			struct Done
			{
				glm::ivec3 Key{ 0 };
				uint64_t Shape = 0;
				uint64_t Edits = 0;
				uint64_t Placement = 0;
				std::shared_ptr<const PreparedShape> Prepared;
				std::shared_ptr<const TerrainChunkSamples> Samples;
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
			Entity Owner;
			// One or the other
			CharacterHandle Character;
			BodyHandle Body;
		};

		float DistanceToChunk(const glm::vec3& point, uint64_t packed, float size)
		{
			const glm::vec3 corner = glm::vec3(UnpackKey(packed)) * size;
			return glm::length(point - glm::clamp(point, corner, corner + size));
		}

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

	void TerrainCollision::PutBody(PhysicsWorld& physics, TerrainState& state, uint64_t packed, const ChunkBody& made,
		const std::shared_ptr<const PreparedShape>& prepared, const glm::mat4& toWorld, const glm::quat& rotation, float size)
	{
		ChunkBody chunk = made;
		if (prepared)
		{
			BodyDesc desc;
			desc.Prepared = prepared;
			desc.Motion = EBodyMotion::Static;
			desc.Position = glm::vec3(toWorld * glm::vec4(glm::vec3(UnpackKey(packed)) * size, 1.0f));
			desc.Rotation = rotation;
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
		state.Waiting.clear();
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
		// Bakes without their values (the edits loaded or put back), those of the copy the bodies are
		// made from, the rest a few at a time
		constexpr size_t BakesMadeAStep = 32;
		SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(state.Owner);
		PrepareTerrainWorldPlan(terrain);
		RestoreTerrainBakes(terrain, state.Snapshot.get(), BakesMadeAStep);
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
			state.Edits = HashTerrainEdits(terrain);
			state.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
			state.LiveSnapshot = state.Snapshot;
			state.Fields.Forget();
			state.Keeps.Forget();
			state.DrawnKeeps.Forget();
			state.EditRuns.clear();
			HashTerrainEditRuns(terrain, state.EditRuns);
			state.EditIndex.Update(terrain, 0);
			state.Waiting.clear();
			for (auto& [key, job] : jobs.InFlight)
			{
				Workers->Cancel(job.Job);
			}
			jobs.InFlight.clear();
		}
		// New edits: what was known of the chunks carried over, rewound to the first edit that
		// changed (an undo); the jobs go on, each kept if its edits are those its chunk is wanted with
		else if (const size_t same = CountSameTerrainEdits(terrain, *state.Snapshot); same != terrain.Edits.size() || same != state.Snapshot->Edits.size())
		{
			state.Fields.Rewind(terrain, same);
			state.EditRuns.resize(same + 1);
			HashTerrainEditRuns(terrain, state.EditRuns);
			state.EditIndex.Update(terrain, same);
			state.Edits = HashTerrainEdits(terrain);
			state.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
		}
		if (!world.Has<TerrainEditActivationComponent>(state.Owner))
		{
			world.Add<TerrainEditActivationComponent>(state.Owner, TerrainEditActivationComponent{});
		}
		const double clock = GetTerrainEditClock();
		NoteTerrainEdits(world.Get<TerrainEditActivationComponent>(state.Owner), ETerrainEditPart::Collision, state.Edits, clock);
		auto field = [&](uint64_t packed) -> const TerrainChunkField& { return state.Fields.Get(*state.Snapshot, { level, UnpackKey(packed) }, &state.EditIndex); };

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
					movers.push_back({ glm::vec3(toTerrain * glm::vec4(moving.Position, 1.0f)), turnToTerrain * moving.Velocity, true, entity, character, {} });
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
					physics.IsAwake(body), entity, {}, body });
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
		auto hasSurface = [&](uint64_t packed) { return field(packed).HasSurface(); };

		// The chunks drawn at this level with no other level round it, whose meshes are the bodies'
		struct Drawn
		{
			std::shared_ptr<const MeshData> Mesh;
			uint64_t Edits = 0;
			std::shared_ptr<const TerrainChunkSamples> Samples;
		};
		std::unordered_map<uint64_t, Drawn> drawn;
		if (world.Has<TerrainChunksComponent>(state.Owner))
		{
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(state.Owner).Chunks)
			{
				if (chunk.bDrawn && chunk.Level == level && chunk.Shape == state.Shape && chunk.Neighbourhood == TerrainNeighbourhood{} && chunk.Mesh.Mesh && chunk.Samples)
				{
					drawn.emplace(PackKey(chunk.Key), Drawn{ chunk.Mesh.Mesh, chunk.Edits, chunk.Samples });
				}
			}
		}
		// The edits a chunk's body is wanted with: its body's while no edit since can change it, else
		// the drawn chunk's while none can change that, else those reaching it now
		auto wantedEdits = [&](uint64_t packed) -> uint64_t
		{
			const uint64_t now = field(packed).GetEdits();
			const TerrainChunkId id{ level, UnpackKey(packed) };
			if (const auto resident = state.Chunks.find(packed); resident != state.Chunks.end() && resident->second.Shape == state.Shape && resident->second.Samples)
			{
				const uint64_t kept = state.Keeps.Get(*state.Snapshot, state.EditRuns, id, resident->second.Edits, resident->second.Samples.get(), now);
				if (kept == resident->second.Edits)
				{
					return kept;
				}
			}
			if (const auto found = drawn.find(packed); found != drawn.end())
			{
				return state.DrawnKeeps.Get(*state.Snapshot, state.EditRuns, id, found->second.Edits, found->second.Samples.get(), now);
			}
			return now;
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

		// Bodies of other edits than their chunks' now: one the edits left without surface gets an
		// empty body, to go in with the others; those only kept round the bodies are made again too,
		// after those wanted
		for (const auto& [packed, chunk] : state.Chunks)
		{
			const uint64_t edits = wantedEdits(packed);
			if (chunk.Shape != state.Shape || chunk.Edits == edits)
			{
				continue;
			}
			if (!hasSurface(packed))
			{
				state.Waiting[packed] = TerrainState::Held{ nullptr, edits, 0, false, nullptr };
			}
			else if (!wanted.contains(packed))
			{
				order.push_back({ std::numeric_limits<float>::max(), packed });
			}
		}

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
		TerrainEditActivationComponent& activation = world.Get<TerrainEditActivationComponent>(state.Owner);
		const bool bLive = activation.Live == state.Edits;
		// The chunks whose bodies new edits changed this step
		std::vector<uint64_t> changed;
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
			const auto job = jobs.InFlight.find(packed);
			if (job != jobs.InFlight.end() && job->second.Edits == done.Edits)
			{
				jobs.InFlight.erase(job);
			}
			state.WorstSeconds = std::max(state.WorstSeconds, std::chrono::duration<double>(now - done.Asked).count());
			if ((!wanted.contains(packed) && !kept.contains(packed)) || done.Edits != wantedEdits(packed))
			{
				continue;
			}
			ChunkBody chunk;
			chunk.Shape = done.Shape;
			chunk.Edits = done.Edits;
			chunk.Triangles = done.Triangles;
			chunk.Samples = done.Samples;
			// In place of a body of other edits: held until these edits are live
			const auto resident = state.Chunks.find(packed);
			if (!bLive && resident != state.Chunks.end() && resident->second.Shape == state.Shape && resident->second.Edits != done.Edits)
			{
				state.Waiting[packed] = TerrainState::Held{ done.Prepared, done.Edits, done.Triangles, done.bShared, done.Samples };
				continue;
			}
			if (resident != state.Chunks.end() && resident->second.Shape == state.Shape && resident->second.Edits != done.Edits)
			{
				changed.push_back(packed);
			}
			state.Waiting.erase(packed);
			PutBody(physics, state, packed, chunk, done.Prepared, toWorld, transform.Rotation, size);
			++state.Made;
			state.Shared += done.bShared ? 1u : 0u;
			++made;
		}
		if (!later.empty())
		{
			std::lock_guard lock(jobs.Lock);
			jobs.Finished.insert(jobs.Finished.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
		}

		// The edits live once the drawn chunks have them too, or by the deadline: the bodies held go in
		// all at once
		bool bReady = true;
		for (const auto& [packed, chunk] : state.Chunks)
		{
			const uint64_t edits = wantedEdits(packed);
			if (chunk.Shape == state.Shape && chunk.Edits != edits)
			{
				const auto waiting = state.Waiting.find(packed);
				bReady = bReady && waiting != state.Waiting.end() && waiting->second.Edits == edits;
			}
		}
		bool bGo = activation.Live == state.Edits;
		if (!bGo && bReady)
		{
			bGo = ActivateTerrainEdits(activation, ETerrainEditPart::Collision, state.Edits, clock, terrain.ActivationDeadline);
		}
		const bool bLate = activation.PendingSince >= 0.0 && clock - activation.PendingSince > double(std::max(terrain.ActivationDeadline, 0.0f));
		if (bGo || bLate)
		{
			for (const auto& [packed, held] : state.Waiting)
			{
				if (held.Edits != wantedEdits(packed))
				{
					continue;
				}
				ChunkBody chunk;
				chunk.Shape = state.Shape;
				chunk.Edits = held.Edits;
				chunk.Triangles = held.Triangles;
				chunk.Samples = held.Samples;
				PutBody(physics, state, packed, chunk, held.Prepared, toWorld, transform.Rotation, size);
				++state.Made;
				state.Shared += held.bShared ? 1u : 0u;
				changed.push_back(packed);
			}
			state.Waiting.clear();
		}

		// What they did to the bodies round them, against the values last live: a character the
		// terrain is in now pushed out, its lower or upper ball whichever is deeper, again until both
		// are out; a dynamic body whose middle it is in left to the physics to push out; one that had
		// terrain under it and hasn't now, lost its support; the dynamic bodies there all woken
		std::vector<TerrainEditEvent> events;
		const glm::vec3 up = glm::normalize(turnToTerrain * glm::vec3(0.0f, 1.0f, 0.0f));
		for (const Mover& mover : changed.empty() ? std::vector<Mover>{} : movers)
		{
			const bool bCharacter = mover.Character.IsValid();
			const float height = bCharacter ? std::max(world.Get<CharacterComponent>(mover.Owner).Height, 0.0f) : 0.0f;
			const float radius = bCharacter ? std::max(world.Get<CharacterComponent>(mover.Owner).Radius, 0.01f) : 0.0f;
			const float margin = height + radius + LookaheadMargin;
			if (std::none_of(changed.begin(), changed.end(), [&](uint64_t packed) { return DistanceToChunk(mover.Position, packed, size) <= margin; }))
			{
				continue;
			}
			// The edits reaching round it alone, the same bits there, far fewer to sample
			const float around = height + radius + PushOutReach + SupportDepth + 1.0f;
			SdfTerrainComponent local = *state.Snapshot;
			KeepTerrainEditsReaching(local, mover.Position - around, mover.Position + around);
			SdfTerrainComponent before = *state.LiveSnapshot;
			KeepTerrainEditsReaching(before, mover.Position - around, mover.Position + around);
			if (!bCharacter)
			{
				physics.Wake(mover.Body);
			}

			glm::vec3 position = mover.Position;
			bool bInside = false;
			for (int pass = 0; pass < (bCharacter ? 3 : 1); ++pass)
			{
				const glm::vec3 lower = position + up * radius;
				const glm::vec3 upper = position + up * std::max(height - radius, radius);
				const float lowerValue = SampleTerrainValue(local, lower);
				const float upperValue = SampleTerrainValue(local, upper);
				// A character half its radius in, a body's middle in
				const float inside = 0.5f * radius;
				if (lowerValue >= inside && upperValue >= inside)
				{
					break;
				}
				bInside = true;
				if (!bCharacter)
				{
					break;
				}
				const glm::vec3 ball = lowerValue <= upperValue ? lower : upper;
				std::optional<glm::vec3> free = FindTerrainFreePoint(local, ball, radius, height + PushOutReach);
				// Where the way out leads nowhere, straight up: out of a box heaped on the ground the
				// nearest face can be the one on the ground, below which the way out turns back
				for (float lift = 0.1f; !free && lift <= height + PushOutReach; lift += 0.1f)
				{
					free = FindTerrainFreePoint(local, ball + up * lift, radius, 0.0f);
				}
				if (!free)
				{
					break;
				}
				position += *free - ball;
			}
			const glm::vec3 from = glm::vec3(toWorld * glm::vec4(mover.Position, 1.0f));
			if (bInside)
			{
				const glm::vec3 to = glm::vec3(toWorld * glm::vec4(position, 1.0f));
				if (bCharacter && position != mover.Position)
				{
					physics.TeleportCharacter(mover.Character, to);
				}
				events.push_back({ mover.Owner, ETerrainEditEvent::InsideNewTerrain, from, to });
				++state.InsideNewTerrain;
				continue;
			}
			// Support: terrain under it, within SupportDepth, then and not now
			bool bLost = false;
			for (float depth = 0.25f; depth <= SupportDepth && !bLost; depth += 0.25f)
			{
				const glm::vec3 below = mover.Position - up * depth;
				bLost = SampleTerrainValue(before, below) < 0.0f && SampleTerrainValue(local, below) >= 0.0f;
			}
			if (bLost)
			{
				events.push_back({ mover.Owner, ETerrainEditEvent::LostSupport, from, from });
				++state.LostSupport;
			}
		}
		if (bGo || bLate)
		{
			state.LiveSnapshot = state.Snapshot;
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
		// drawn where the terrain's chunks have it at this level with no other level round it, of the
		// edits wanted
		const uint32_t maxJobs = std::max(Workers->GetWorkerCount(), 1u);
		for (const auto& [time, packed] : order)
		{
			const uint64_t edits = wantedEdits(packed);
			const auto resident = state.Chunks.find(packed);
			const auto waiting = state.Waiting.find(packed);
			if ((resident != state.Chunks.end() && resident->second.Shape == state.Shape && resident->second.Edits == edits) ||
				(waiting != state.Waiting.end() && waiting->second.Edits == edits))
			{
				continue;
			}
			if (const auto job = jobs.InFlight.find(packed); job != jobs.InFlight.end())
			{
				if (job->second.Edits == edits || !Workers->Cancel(job->second.Job))
				{
					continue;
				}
				jobs.InFlight.erase(job);
			}
			if (jobs.InFlight.size() >= maxJobs)
			{
				break;
			}
			std::shared_ptr<Detail::TerrainCollisionJobs> shared = state.Jobs;
			std::shared_ptr<const SdfTerrainComponent> snapshot = state.Snapshot;
			const auto found = drawn.find(packed);
			const bool bShare = found != drawn.end() && found->second.Edits == edits;
			std::shared_ptr<const MeshData> mesh = bShare ? found->second.Mesh : nullptr;
			std::shared_ptr<const TerrainChunkSamples> sharedSamples = bShare ? found->second.Samples : nullptr;
			const glm::ivec3 key = UnpackKey(packed);
			const uint64_t jobShape = state.Shape;
			const uint64_t jobEdits = edits;
			// In place of a body of other edits: soonest, its parts on every worker free
			const bool bEdit = resident != state.Chunks.end() && resident->second.Shape == state.Shape && resident->second.Edits != edits;
			FieldRunParts runParts;
			if (bEdit)
			{
				JobSystem* workers = Workers;
				runParts = [workers](size_t count, const std::function<void(size_t)>& part) { workers->ParallelFor(count, part); };
			}
			const uint64_t jobPlacement = state.Placement;
			const glm::vec3 scale = transform.Scale;
			const auto asked = std::chrono::steady_clock::now();
			JobHandle job = Workers->Schedule(
				[shared, snapshot, mesh, sharedSamples, key, level, jobShape, jobEdits, jobPlacement, scale, asked, runParts]
				{
					Detail::TerrainCollisionJobs::Done done;
					done.Key = key;
					done.Shape = jobShape;
					done.Edits = jobEdits;
					done.Placement = jobPlacement;
					done.Asked = asked;
					done.bShared = mesh != nullptr;
					auto samples = std::make_shared<TerrainChunkSamples>();
					const MeshData made = mesh ? MeshData{} : MakeTerrainChunkMesh(*snapshot, { level, key }, {}, nullptr, samples.get(), runParts);
					done.Samples = mesh ? sharedSamples : std::move(samples);
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
				bEdit ? EJobPriority::High : EJobPriority::Normal);
			jobs.InFlight.emplace(packed, Detail::TerrainCollisionJobs::Scheduled{ job, jobShape, jobEdits });
		}

		// What it did, and the bodies collision can't keep up with
		TerrainCollisionStats stats;
		stats.Wanted = static_cast<uint32_t>(order.size());
		stats.Skipped = skipped;
		stats.Pending = static_cast<uint32_t>(jobs.InFlight.size());
		stats.Made = state.Made;
		stats.Shared = state.Shared;
		stats.WorstSeconds = state.WorstSeconds;
		stats.Waiting = static_cast<uint32_t>(state.Waiting.size());
		stats.InsideNewTerrain = state.InsideNewTerrain;
		stats.LostSupport = state.LostSupport;
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
		world.Get<TerrainCollisionComponent>(state.Owner).Events = std::move(events);
	}
}

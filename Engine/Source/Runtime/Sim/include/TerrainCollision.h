#pragma once

#include "Entity.h"
#include "PhysicsWorld.h"
#include "TerrainChunks.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class JobSystem;
	class PhysicsBodies;
	struct SdfTerrainComponent;

	namespace Detail
	{
		struct TerrainCollisionJobs;
	}

	// The SDF terrains' collision in a game world, each fixed step before the physics step
	// (GameWorld): a static body for each chunk of the terrain's finest level (TerrainChunks.h) the
	// moving bodies need, its triangles those the chunk is drawn with at that level.
	// - Wanted: the chunks within Collision Distance (SdfTerrainComponent::CollisionDistance) of every
	//   character and awake dynamic body, and within a few metres of where each is going over the next
	//   LookaheadSeconds, by its velocity and gravity; those round it needed as soon as it could
	//   reach them at its speed, so a fast body's way ahead comes before what it passes by. A sleeping
	//   body keeps those it rests in. Chunks all air or all solid get none.
	// - Made on the job system's workers, soonest first, a few at a time: the chunk's mesh (the one
	//   drawn, when the terrain's chunks have it at the finest level against chunks of that level all
	//   round, else made as it would be), and its shape (PrepareBodyShape()); its body is made here,
	//   a few a step, in place of the one it replaces. Bodies a chunk farther than wanted go.
	// - New values make every chunk again, the old bodies staying until each one's new body is in;
	//   the terrain entity moved makes them all again where it is now; a terrain gone takes its bodies.
	// - New edits make the chunks they reach again (TerrainChunkField), the new bodies held until the
	//   drawn chunks have them too, or the terrain's Activation Deadline (TerrainEditActivationComponent),
	//   then put in all at once. Then, by the values before and after: a character the edits put
	//   inside the terrain is pushed out (FindTerrainFreePoint()); a dynamic body whose middle they
	//   did is left to the physics to push out; one they took the terrain from under has lost its
	//   support; each is told on the terrain's TerrainCollisionComponent (TerrainEditEvent) for that
	//   step, and every dynamic body round the chunks changed is woken.
	// - A body in or next to a wanted chunk without its body yet is uncovered; one whose way ahead
	//   reaches such a chunk sooner than a chunk lately took to make is outrunning collision. Both are
	//   counted on the terrain's TerrainCollisionComponent, for the game to slow or hold them.
	// Which chunks have bodies depends on how fast the workers are: a world stepped on another machine
	// may have collision a step sooner or later.
	class TerrainCollision
	{
	public:
		// The workers the chunks are made on; none: the program's (JobSystem::Get()). It must outlive this.
		explicit TerrainCollision(JobSystem* jobs = nullptr);
		// Cancels what hasn't started; what runs finishes into what it shares, then goes
		~TerrainCollision();
		TerrainCollision(const TerrainCollision&) = delete;
		TerrainCollision& operator=(const TerrainCollision&) = delete;

		void SyncBeforeStep(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies);

		// The terrain whose chunk a body is; nothing for another body
		std::optional<Entity> FindEntity(BodyHandle body) const;
		size_t GetBodyCount() const;

		// How far ahead along its way a moving body's chunks are wanted, in seconds
		static constexpr float LookaheadSeconds = 2.0f;
		// And how near that way, in metres; a body slower than MovingSpeed (m/s) has no way ahead
		static constexpr float LookaheadMargin = 4.0f;
		static constexpr float MovingSpeed = 0.5f;
		// At most so many bodies made in a step
		static constexpr uint32_t MaxBodiesPerStep = 32;
		// How far a character is looked for a place out of new terrain, in metres past its height
		static constexpr float PushOutReach = 2.0f;
		// How far under a body terrain that edits took away is its lost support, in metres
		static constexpr float SupportDepth = 1.0f;

	private:
		struct ChunkBody
		{
			// Invalid for a chunk without triangles
			BodyHandle Body;
			uint64_t Shape = 0;
			uint64_t Edits = 0;
			uint32_t Triangles = 0;
		};

		struct TerrainState
		{
			Entity Owner;
			bool bSeen = false;
			// The values, their edits and the placement the bodies are made from
			uint64_t Shape = 0;
			uint64_t Edits = 0;
			uint64_t Placement = 0;
			std::shared_ptr<const SdfTerrainComponent> Snapshot;
			// The values with the edits last live, to tell what new ones changed round a body
			std::shared_ptr<const SdfTerrainComponent> LiveSnapshot;
			// What is known of each chunk's values, for the values now
			TerrainChunkFields Fields;
			std::unordered_map<uint64_t, ChunkBody> Chunks;
			std::shared_ptr<Detail::TerrainCollisionJobs> Jobs;
			// Bodies made for new edits in place of others, waiting for them to go live; an empty shape
			// for a chunk the edits left without surface
			struct Held
			{
				std::shared_ptr<const PreparedShape> Prepared;
				uint64_t Edits = 0;
				uint32_t Triangles = 0;
				bool bShared = false;
			};
			std::unordered_map<uint64_t, Held> Waiting;
			uint64_t Made = 0;
			uint64_t Shared = 0;
			uint64_t InsideNewTerrain = 0;
			uint64_t LostSupport = 0;
			double WorstSeconds = 0.0;
		};

		void SyncTerrain(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies, TerrainState& state);
		// A chunk's body in place of the one it had, if any; none for an empty shape
		void PutBody(PhysicsWorld& physics, TerrainState& state, uint64_t packed, const ChunkBody& chunk, const std::shared_ptr<const PreparedShape>& prepared,
			const glm::mat4& toWorld, const glm::quat& rotation, float size);
		void Release(PhysicsWorld& physics, TerrainState& state);

		JobSystem* Workers = nullptr;
		std::vector<TerrainState> Terrains;
		std::unordered_map<uint32_t, Entity> BodyOwners;
	};
}

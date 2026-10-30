#pragma once

#include "Entity.h"
#include "PhysicsWorld.h"

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

	private:
		struct ChunkBody
		{
			// Invalid for a chunk without triangles
			BodyHandle Body;
			uint64_t Shape = 0;
			uint32_t Triangles = 0;
		};

		struct TerrainState
		{
			Entity Owner;
			bool bSeen = false;
			// The values and the placement the bodies are made from
			uint64_t Shape = 0;
			uint64_t Placement = 0;
			std::shared_ptr<const SdfTerrainComponent> Snapshot;
			// Whether the surface crosses a chunk, for the values now
			std::unordered_map<uint64_t, bool> Surface;
			std::unordered_map<uint64_t, ChunkBody> Chunks;
			std::shared_ptr<Detail::TerrainCollisionJobs> Jobs;
			uint64_t Made = 0;
			uint64_t Shared = 0;
			double WorstSeconds = 0.0;
		};

		void SyncTerrain(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies, TerrainState& state);
		void Release(PhysicsWorld& physics, TerrainState& state);

		JobSystem* Workers = nullptr;
		std::vector<TerrainState> Terrains;
		std::unordered_map<uint32_t, Entity> BodyOwners;
	};
}

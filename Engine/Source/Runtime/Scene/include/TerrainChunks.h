#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "MeshData.h"
#include "RuntimeMeshComponent.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class JobSystem;

	// A chunk is 32 cells a side. At level of detail 0 its cells are 1 m, each level up twice the
	// size: 32 m chunks at level 0, 64 m at 1, 4 km at 7; level -1 has cells of half a metre.
	constexpr int TerrainChunkCells = 32;
	constexpr float TerrainCellSize = 1.0f;
	constexpr float TerrainChunkSize = float(TerrainChunkCells) * TerrainCellSize;
	// The finest and coarsest levels there are
	constexpr int TerrainFinestLevel = -1;
	constexpr int TerrainCoarsestLevel = 12;

	float GetTerrainCellSize(int level);
	float GetTerrainChunkSize(int level);

	// Which chunk: its level of detail, and where, its corner Key times its level's chunk size in the
	// terrain entity's space
	struct TerrainChunkId
	{
		int Level = 0;
		glm::ivec3 Key{ 0 };

		bool operator==(const TerrainChunkId&) const = default;
	};

	// The levels of what is round a chunk, which its seams are made against: of the 27 places of its
	// own size round it and in the middle itself, x fastest, each finer (-1, its eight chunks of the
	// level below), the same (0), coarser (1, part of a chunk of the level above), or nothing there
	// (None, past the view distance)
	struct TerrainNeighbourhood
	{
		static constexpr int8_t None = 2;
		std::array<int8_t, 27> Levels{};

		int8_t At(const glm::ivec3& offset) const { return Levels[size_t((offset.x + 1) + 3 * ((offset.y + 1) + 3 * (offset.z + 1)))]; }
		int8_t& At(const glm::ivec3& offset) { return Levels[size_t((offset.x + 1) + 3 * ((offset.y + 1) + 3 * (offset.z + 1)))]; }
		bool HasSeams() const;
		uint64_t Hash() const;
		bool operator==(const TerrainNeighbourhood&) const = default;
	};

	struct TerrainChunkSamples;

	// One chunk of an SDF terrain, drawn or made to be drawn next
	struct TerrainChunk
	{
		int Level = 0;
		glm::ivec3 Key{ 0 };
		glm::vec3 Origin{ 0.0f };
		// Around Origin; no data for a chunk the surface passes by
		RuntimeMeshComponent Mesh;
		// What it was made from (HashSdfTerrainShape()), with which edits (TerrainChunkField::GetEdits()
		// when made, kept while later ones can't change it, TerrainChunkKeeps), and against; what it
		// read of the field
		uint64_t Shape = 0;
		uint64_t Edits = 0;
		TerrainNeighbourhood Neighbourhood;
		std::shared_ptr<const TerrainChunkSamples> Samples;
		// Drawn, or uploaded only, waiting for all the chunks it is drawn with
		bool bDrawn = false;
		// The first shadow cascade it casts into (SdfTerrainComponent::NearShadowLevels)
		uint32_t FirstShadowCascade = 0;
	};

	namespace Detail
	{
		struct TerrainStreaming;
	}

	// What the chunks of a terrain are doing, for the editor and the stats
	struct TerrainChunksStats
	{
		// The chunks drawn or to be drawn next that the surface crosses, those drawn, and those being
		// made
		uint32_t Wanted = 0;
		uint32_t Resident = 0;
		uint32_t Pending = 0;
		// Of the chunks drawn or to be drawn, those all air or all solid, never meshed
		uint32_t Skipped = 0;
		uint64_t Triangles = 0;
		// Drawn, by level (from TerrainFinestLevel), and those of them next to one of another level
		std::array<uint32_t, TerrainCoarsestLevel - TerrainFinestLevel + 1> ByLevel{};
		uint32_t WithSeams = 0;
		// The chunks drawn are those chosen for where the viewpoints are, made from the values as
		// they are, and nothing is being chosen or made
		bool bSettled = false;
		// Made since the terrain was added, and made for values or levels that had changed by the time
		// they were done, so let go of
		uint64_t Made = 0;
		uint64_t Dropped = 0;
		// Times all the chunks drawn were replaced at once by the next ones
		uint64_t Swaps = 0;
		// The last chunk's, on its worker, and the last choice of chunks'
		double LastSampleMilliseconds = 0.0;
		double LastMeshMilliseconds = 0.0;
		double LastPlanMilliseconds = 0.0;
	};

	// An SDF terrain's chunks, on the terrain's entity: made by UpdateTerrainChunks(), the drawn ones
	// drawn by the entity's Mesh Renderer, one object each. Run-time only, never saved.
	struct TerrainChunksComponent
	{
		std::vector<TerrainChunk> Chunks;
		TerrainChunksStats Stats;
		// The jobs in flight and what they made, shared with them
		std::shared_ptr<Detail::TerrainStreaming> Streaming;
	};

	// What an SDF terrain's collision is doing while the world runs (TerrainCollision, in the game
	// world's step), for the editor and the stats
	struct TerrainCollisionStats
	{
		// Chunks wanted round the moving bodies and along where they are going, those with a body, and
		// those being made
		uint32_t Wanted = 0;
		uint32_t Resident = 0;
		uint32_t Pending = 0;
		// Wanted chunks all air or all solid, never given a body
		uint32_t Skipped = 0;
		uint64_t Triangles = 0;
		// Bodies in or next to a chunk wanted that has no body yet, and those that will reach one sooner
		// than a chunk takes to make: going faster than collision can follow
		uint32_t Uncovered = 0;
		uint32_t Outrunning = 0;
		// Made since the world began, and those made from a chunk drawn at the finest level
		uint64_t Made = 0;
		uint64_t Shared = 0;
		// The longest a chunk took from being asked for to its body, lately, in seconds
		double WorstSeconds = 0.0;
		// Bodies made for new edits, waiting for them to go live
		uint32_t Waiting = 0;
		// Bodies new terrain was put in, and bodies terrain was taken from under, since the world began
		uint64_t InsideNewTerrain = 0;
		uint64_t LostSupport = 0;
	};

	// What edits going live in collision did to a body (TerrainCollision)
	enum class ETerrainEditEvent : uint8_t
	{
		// The edits put it inside the terrain: a character moved out, From to To; a dynamic body left
		// to the physics, To the same as From
		InsideNewTerrain,
		// They took away the terrain under it
		LostSupport
	};
	struct TerrainEditEvent
	{
		// The character's or the dynamic body's entity
		Entity Body;
		ETerrainEditEvent Kind = ETerrainEditEvent::InsideNewTerrain;
		// Where it was, and where it is now, in the world's space
		glm::vec3 From{ 0.0f };
		glm::vec3 To{ 0.0f };
	};

	// An SDF terrain's collision, on the terrain's entity while the world runs: written by the game
	// world's step, for the details panel and the game. Run-time only, never saved.
	struct TerrainCollisionComponent
	{
		TerrainCollisionStats Stats;
		// What edits that went live this step did to bodies; none the steps they don't
		std::vector<TerrainEditEvent> Events;
	};

	// What the collision is doing, for the editor's details panel
	std::vector<InspectedValue> DescribeTerrainCollision(const TerrainCollisionComponent& component);

	// How long a chunk took, and what it made
	struct TerrainChunkTimes
	{
		double SampleMilliseconds = 0.0;
		double MeshMilliseconds = 0.0;
		uint32_t Triangles = 0;
		uint32_t SeamTriangles = 0;
	};

	// What a chunk's mesh read of the field, to tell an edit that can't change it. The meshers read
	// a sample's value only near the surface (in a cell the surface crosses, or the cell next to one,
	// for its gradient), elsewhere only whether it is inside, and the field itself only round a sharp
	// vertex's crossings and along the sky's rays, whether it is inside. Its vertices are in the
	// cells the surface crosses, and its sky's rays go from half a cell off them up to SkyReach, at
	// least 45 degrees up. So: its grids' bricks of 4 x 4 x 4 cells near the surface (one the surface
	// crosses and those next to it), each with the lowest and highest of its samples and whether the
	// surface crosses it; the points it asked the field at in its grids, in bricks the same size, each
	// with the box of its points and the lowest and highest the field was there; and its grids' box.
	struct TerrainChunkSamples
	{
		struct Brick
		{
			glm::vec3 Min{ 0.0f };
			glm::vec3 Max{ 0.0f };
			float Lowest = 0.0f;
			float Highest = 0.0f;
			bool bCrossed = false;
			// Points the mesher asked the field at, not samples
			bool bAsked = false;
		};
		std::vector<Brick> NearSurface;
		// The bricks' bounds together, round those asked one cell farther and over the values
		// there, to tell most edits apart at once
		glm::vec3 NearMin{ 0.0f };
		glm::vec3 NearMax{ 0.0f };
		float NearLowest = 0.0f;
		float NearHighest = 0.0f;
		glm::vec3 GridMin{ 0.0f };
		glm::vec3 GridMax{ 0.0f };
		// Its finer grid's cell, and how far its sky's rays go
		float CellSize = 1.0f;
		float SkyReach = 0.0f;
		// How many of the terrain's edits it was made with, and their hash (HashTerrainEditRuns())
		size_t Edits = 0;
		uint64_t EditsHash = 0;
	};

	// Whether an edit after those a chunk was made with can change its mesh: whether it can turn
	// the field's sign in its grids or up a sky's ray from a brick the surface crosses (a box the
	// edit's sign bounds, GetTerrainEditSignBounds(), against the cone 45 degrees up from the brick),
	// or change a sample in a brick near the surface or a value the mesher asked the field for. One
	// that can't leaves the mesh the same bits.
	bool CanTerrainEditChangeChunk(const TerrainChunkSamples& samples, const SdfTerrainEdit& edit);

	// Which chunks made earlier are still the mesh they would be made now, carried edit to edit:
	// a chunk is wanted with the edits it was made with (its TerrainChunk::Edits) while no edit since
	// can change it (CanTerrainEditChangeChunk()), else with those reaching it now. Kept by chunk;
	// Forget() when the values changed.
	class TerrainChunkKeeps
	{
	public:
		// `madeEdits` and `samples` are the chunk's as made, `now` the edits reaching it now
		// (TerrainChunkField::GetEdits()), `runs` the terrain's HashTerrainEditRuns()
		uint64_t Get(const SdfTerrainComponent& terrain, const std::vector<uint64_t>& runs, const TerrainChunkId& id, uint64_t madeEdits,
			const TerrainChunkSamples* samples, uint64_t now);
		void Forget() { Known.clear(); }

	private:
		struct Kept
		{
			uint64_t Edits = 0;
			// The edits looked at so far, by the hash of those up to there
			size_t Seen = 0;
			uint64_t SeenHash = 0;
			bool bChanged = false;
		};
		std::unordered_map<uint64_t, Kept> Known;
	};

	// The mesh of a chunk: the terrain sampled at its level from two cells before its corner to one
	// past its far side, meshed by the terrain's mesher (the dual one with the terrain itself for its sharp
	// vertices' planes), its positions relative to its corner, its vertices coloured by their
	// material (MaterialColors) or as the debug view says. Next to a chunk of another level it has
	// seams (FieldSeams): next to a coarser one its samples on their shared faces are the coarser
	// ones' and between them their average, and the dual mesh joins its cells to the coarser ones';
	// next to a finer one marching cubes meshes its cells there with the finer samples. Chunks side
	// by side meet without a gap, at one level or two. A function of the terrain's values, the id and
	// the neighbourhood alone. With `outSamples`, what it read (TerrainChunkSamples); with `runParts`,
	// its sampling and its sky in parts on other threads, the same bits.
	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& terrain, const TerrainChunkId& id, const TerrainNeighbourhood& neighbourhood = {},
		TerrainChunkTimes* outTimes = nullptr, TerrainChunkSamples* outSamples = nullptr, const FieldRunParts& runParts = {});

	// A hash of every value a chunk's mesh is made from: all but those choosing the chunks
	// (ViewDistance, LevelDistance, FinestLevel) and their shadows, and the edits, which each chunk
	// tells apart by those reaching it (TerrainChunkField)
	uint64_t HashSdfTerrainShape(const SdfTerrainComponent& terrain);

	// What is known of a chunk's values, carried across edits (TerrainBoxEdits): over the samples its
	// mesh is made from, whether the surface may cross them; over all the field its mesh can sample
	// (those, the coarser grid round it, and its vertices' sky, SkyReach past it), the edits that can
	// change it, which a chunk made with them is told apart by. MakeTerrainChunkMesh() samples a copy
	// of the terrain with only those.
	struct TerrainChunkField
	{
		TerrainBoxEdits Surface;
		TerrainBoxEdits Reach;

		bool HasSurface() const { return !Surface.Range.IsAllAir() && !Surface.Range.IsAllSolid(); }
		uint64_t GetEdits() const { return Reach.Reach.Hash; }
	};
	TerrainChunkField BeginTerrainChunkField(const SdfTerrainComponent& terrain, const TerrainChunkId& id);
	void GetTerrainChunkReach(const TerrainChunkId& id, glm::vec3& outMin, glm::vec3& outMax);

	// Each chunk's field as it is asked for, for one terrain's values: kept, and continued over the
	// edits added since (one shape's range an edit; through the index, only those near), so a chunk
	// costs its one sample once. When edits it has seen changed or went, Rewind() to how many are
	// the same (CountSameTerrainEdits()): an undo costs what an edit does. Forget() for new values.
	class TerrainChunkFields
	{
	public:
		const TerrainChunkField& Get(const SdfTerrainComponent& terrain, const TerrainChunkId& id, const TerrainEditIndex* index = nullptr);
		void Rewind(const SdfTerrainComponent& terrain, size_t same);
		void Forget() { Known.clear(); }

	private:
		std::unordered_map<uint64_t, TerrainChunkField> Known;
	};

	// Edits made live in drawing and in collision together: each part holds what it made for new
	// edits until the other has made its own too, or the terrain's Activation Deadline has passed since
	// they came, so a hole is neither seen before it can be fallen into nor fallen into before it is
	// seen. A part that hasn't run for a while (collision outside the game) isn't waited for. On the
	// terrain's entity, kept by its chunks (UpdateTerrainChunks()) and its collision
	// (TerrainCollision). Run-time only, never saved.
	struct TerrainEditActivationComponent
	{
		// The edits as last seen (HashTerrainEdits()), those each part has made all it needs for, and
		// those live in both; 0: none yet
		uint64_t Latest = 0;
		uint64_t DrawReady = 0;
		uint64_t CollisionReady = 0;
		uint64_t Live = 0;
		// When edits not live yet came, and when each part last ran, in seconds of the steady clock;
		// below 0: never
		double PendingSince = -1.0;
		double DrawSeen = -1.0;
		double CollisionSeen = -1.0;
		// Edits made live, those of them by the deadline with a part not ready, and the last one's
		// time from its edits coming to live
		uint64_t Activations = 0;
		uint64_t Late = 0;
		double LastSeconds = 0.0;
	};

	enum class ETerrainEditPart : uint8_t
	{
		Draw,
		Collision
	};

	// How long a part may not run before it isn't waited for, in seconds
	constexpr double TerrainEditPartTimeout = 0.5;

	// A part's edits as it knows them, each time it runs; the first edits seen are live already
	void NoteTerrainEdits(TerrainEditActivationComponent& activation, ETerrainEditPart part, uint64_t edits, double now);

	// The part has made all it needs for `edits`: whether they are live, made so now if the other part
	// has made its own, isn't running, or the deadline has passed since they came
	bool ActivateTerrainEdits(TerrainEditActivationComponent& activation, ETerrainEditPart part, uint64_t edits, double now, float deadline);

	// The steady clock in seconds, what the activation's times are in
	double GetTerrainEditClock();

	// What the activation is doing, for the editor's details panel
	std::vector<InspectedValue> DescribeTerrainEditActivation(const TerrainEditActivationComponent& component);

	// Which chunks are drawn round viewpoints, at which levels
	struct TerrainLevelSettings
	{
		float ViewDistance = 1024.0f;
		// How far each level reaches, in its own chunks (SdfTerrainComponent::LevelDistance)
		float LevelDistance = 4.0f;
		int FinestLevel = 0;
		// How much farther than where a chunk splits into the eight of the level below they join
		// back into it, as a part of that distance
		float Hysteresis = 0.2f;
	};

	// The chunks chosen, an octree's leaves: every one within the view distance of a viewpoint, each
	// level from the coarsest down split where a viewpoint is nearer its chunk than LevelDistance of
	// the level below's chunks; balanced, so chunks that touch, even at a corner, are at most one
	// level apart. Those that were split stay split until a viewpoint is farther by the hysteresis.
	struct TerrainChunkTree
	{
		std::vector<TerrainChunkId> Leaves;
		// The chunks split into the level below, for the hysteresis next time
		std::vector<TerrainChunkId> Split;
	};
	TerrainChunkTree SelectTerrainChunks(std::span<const glm::vec3> viewpoints, const TerrainLevelSettings& settings, const TerrainChunkTree* previous = nullptr);

	// What is round each of the tree's leaves, in their order
	std::vector<TerrainNeighbourhood> GetTerrainNeighbourhoods(const TerrainChunkTree& tree);

	// Every SDF terrain's chunks, each frame, in the editor and the game: those of the chunk tree round
	// the viewpoints (in the world's space; SelectTerrainChunks(), chosen on a worker) that the surface
	// crosses (their range, GetTerrainValueRange(), is neither all air nor all solid) are meshed on the
	// job system's workers, nearest first, a few at a time, uploaded a few a frame while the chunks
	// drawn stay drawn, then all drawn at once in their place, so their seams always match; then the
	// next tree is chosen if a viewpoint moved. New values start again from the chunks drawn. New
	// edits choose the tree again and make its chunks they reach again; on the tree drawn, those of
	// the finest two levels drawn go in as soon as they are all made and collision has its own
	// (TerrainEditActivationComponent), the coarser ones once they are made. The entity gets its
	// TerrainChunksComponent here.
	void UpdateTerrainChunks(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs);

	// The terrain's remove step: its chunks go with it, and its jobs are cancelled
	void RemoveTerrainChunks(Registry& world, Entity entity);

	// What the chunks are doing, for the editor's details panel
	std::vector<InspectedValue> DescribeTerrainChunks(const TerrainChunksComponent& component);
}

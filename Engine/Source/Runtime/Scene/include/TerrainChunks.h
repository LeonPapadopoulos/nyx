#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "MeshData.h"
#include "RuntimeMeshComponent.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
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

	// One chunk of an SDF terrain, drawn or made to be drawn next
	struct TerrainChunk
	{
		int Level = 0;
		glm::ivec3 Key{ 0 };
		glm::vec3 Origin{ 0.0f };
		// Around Origin; no data for a chunk the surface passes by
		RuntimeMeshComponent Mesh;
		// What it was made from (HashSdfTerrainShape()) and against
		uint64_t Shape = 0;
		TerrainNeighbourhood Neighbourhood;
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

	// How long a chunk took, and what it made
	struct TerrainChunkTimes
	{
		double SampleMilliseconds = 0.0;
		double MeshMilliseconds = 0.0;
		uint32_t Triangles = 0;
		uint32_t SeamTriangles = 0;
	};

	// The mesh of a chunk: the terrain sampled at its level from two cells before its corner to one
	// past its far side, meshed by the terrain's mesher (the dual one with the terrain itself for its sharp
	// vertices' planes), its positions relative to its corner, its vertices coloured by their
	// material (MaterialColors) or as the debug view says. Next to a chunk of another level it has
	// seams (FieldSeams): next to a coarser one its samples on their shared faces are the coarser
	// ones' and between them their average, and the dual mesh joins its cells to the coarser ones';
	// next to a finer one marching cubes meshes its cells there with the finer samples. Chunks side
	// by side meet without a gap, at one level or two. A function of the terrain's values, the id and
	// the neighbourhood alone.
	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& terrain, const TerrainChunkId& id, const TerrainNeighbourhood& neighbourhood = {},
		TerrainChunkTimes* outTimes = nullptr);

	// A hash of every value a chunk's mesh is made from: all but those choosing the chunks
	// (ViewDistance, LevelDistance, FinestLevel) and their shadows
	uint64_t HashSdfTerrainShape(const SdfTerrainComponent& terrain);

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
	// next tree is chosen if a viewpoint moved. New values start again from the chunks drawn. The
	// entity gets its TerrainChunksComponent here.
	void UpdateTerrainChunks(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs);

	// The terrain's remove step: its chunks go with it, and its jobs are cancelled
	void RemoveTerrainChunks(Registry& world, Entity entity);

	// What the chunks are doing, for the editor's details panel
	std::vector<InspectedValue> DescribeTerrainChunks(const TerrainChunksComponent& component);
}

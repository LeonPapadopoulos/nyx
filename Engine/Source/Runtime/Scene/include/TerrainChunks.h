#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "MeshData.h"
#include "RuntimeMeshComponent.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Nyx
{
	class JobSystem;

	// A chunk is 32 cells of 1 m a side, at one level of detail for now
	constexpr int TerrainChunkCells = 32;
	constexpr float TerrainCellSize = 1.0f;
	constexpr float TerrainChunkSize = float(TerrainChunkCells) * TerrainCellSize;

	// One chunk of an SDF terrain as it is drawn
	struct TerrainChunk
	{
		// Which chunk: its corner is Key times TerrainChunkSize in the terrain entity's space
		glm::ivec3 Key{ 0 };
		glm::vec3 Origin{ 0.0f };
		// Around Origin; no data for a chunk the surface passes by
		RuntimeMeshComponent Mesh;
		// What it was made from (HashSdfTerrainShape()): made again when the terrain changes
		uint64_t Shape = 0;
	};

	namespace Detail
	{
		struct TerrainStreaming;
	}

	// What the chunks of a terrain are doing, for the editor and the stats
	struct TerrainChunksStats
	{
		// Within the view distance and crossed by the surface
		uint32_t Wanted = 0;
		uint32_t Resident = 0;
		uint32_t Pending = 0;
		// Within the view distance and all air or all solid, so never meshed
		uint32_t Skipped = 0;
		uint64_t Triangles = 0;
		// Made since the terrain was added, and made for values that had changed by the time they
		// were done, so let go of
		uint64_t Made = 0;
		uint64_t Dropped = 0;
		// The last chunk's, on its worker
		double LastSampleMilliseconds = 0.0;
		double LastMeshMilliseconds = 0.0;
	};

	// An SDF terrain's chunks, on the terrain's entity: made by UpdateTerrainChunks(), drawn by the
	// entity's Mesh Renderer, one object each. Run-time only, never saved.
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
	};

	// The mesh of the chunk at `key`: the terrain sampled from two cells before the chunk's corner to
	// one past its far side, meshed by the terrain's mesher (the dual one with the terrain itself for
	// its sharp vertices' planes), its positions relative to the chunk's corner, its vertices coloured
	// by their material (MaterialColors). Chunks side by side meet without a gap. A function of the
	// terrain's values and the key alone.
	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& terrain, const glm::ivec3& key, TerrainChunkTimes* outTimes = nullptr);

	// A hash of every value a chunk's mesh is made from: all but the view distance
	uint64_t HashSdfTerrainShape(const SdfTerrainComponent& terrain);

	// Every SDF terrain's chunks, each frame, in the editor and the game: those within the view
	// distance of any of the viewpoints (in the world's space) that the surface crosses (their range,
	// GetTerrainValueRange(), is neither all air nor all solid) are wanted; the nearest missing ones,
	// or those made from values that have since changed, are meshed on the job system's workers, a
	// few at a time; what they made goes in, at most so many a frame, each replacing its old mesh only
	// once its new one is ready, so nothing is ever missing while it is made again; chunks a chunk
	// beyond the view distance go. The entity gets its TerrainChunksComponent here.
	void UpdateTerrainChunks(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs);

	// The terrain's remove step: its chunks go with it, and its jobs are cancelled
	void RemoveTerrainChunks(Registry& world, Entity entity);

	// What the chunks are doing, for the editor's details panel
	std::vector<InspectedValue> DescribeTerrainChunks(const TerrainChunksComponent& component);
}

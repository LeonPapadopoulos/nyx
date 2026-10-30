#include "NyxPCH.h"
#include "TerrainChunks.h"

#include "FieldMesher.h"
#include "JobSystem.h"
#include "SdfTerrain.h"
#include "TransformComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Nyx
{
	namespace
	{
		uint64_t PackId(int level, const glm::ivec3& key)
		{
			auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 19)) & 0xFFFFFu); };
			return (uint64_t(level - TerrainFinestLevel) << 60) | part(key.x) | (part(key.y) << 20) | (part(key.z) << 40);
		}

		uint64_t PackId(const TerrainChunkId& id)
		{
			return PackId(id.Level, id.Key);
		}

		TerrainChunkId UnpackId(uint64_t packed)
		{
			auto part = [&](int shift) { return int((packed >> shift) & 0xFFFFFu) - (1 << 19); };
			return { int(packed >> 60) + TerrainFinestLevel, glm::ivec3(part(0), part(20), part(40)) };
		}

		glm::ivec3 ChildKey(const glm::ivec3& key, int child)
		{
			return key * 2 + glm::ivec3(child & 1, (child >> 1) & 1, (child >> 2) & 1);
		}

		glm::ivec3 NeighbourOffset(int index)
		{
			return { index % 3 - 1, (index / 3) % 3 - 1, index / 9 - 1 };
		}

		glm::vec3 ChunkCorner(const TerrainChunkId& id)
		{
			return glm::vec3(id.Key) * GetTerrainChunkSize(id.Level);
		}

		float DistanceToChunk(const glm::vec3& point, const TerrainChunkId& id)
		{
			const glm::vec3 low = ChunkCorner(id);
			const glm::vec3 nearest = glm::clamp(point, low, low + GetTerrainChunkSize(id.Level));
			return glm::length(point - nearest);
		}

		float NearestViewpoint(std::span<const glm::vec3> viewpoints, const TerrainChunkId& id)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const glm::vec3& viewpoint : viewpoints)
			{
				nearest = std::min(nearest, DistanceToChunk(viewpoint, id));
			}
			return nearest;
		}

		bool IsLexicographicallyBefore(const TerrainChunkId& a, const TerrainChunkId& b)
		{
			return std::tie(a.Level, a.Key.x, a.Key.y, a.Key.z) < std::tie(b.Level, b.Key.x, b.Key.y, b.Key.z);
		}

		int ClampLevel(int level)
		{
			return std::clamp(level, TerrainFinestLevel, TerrainCoarsestLevel);
		}

		struct Hasher
		{
			uint64_t Hash = 0xCBF29CE484222325ull;

			void Bytes(const void* data, size_t size)
			{
				const auto* bytes = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					Hash ^= bytes[i];
					Hash *= 0x100000001B3ull;
				}
			}

			template <typename T>
			void Value(const T& value)
			{
				Bytes(&value, sizeof(T));
			}
		};

		// The places of a chunk's size round it (TerrainNeighbourhood) whose closed boxes have a point,
		// given in the chunk's cells from its corner: on each axis the one it is in, or both where it is
		// on the face between them
		template <typename Visit>
		void ForPlacesAt(const glm::vec3& local, Visit&& visit)
		{
			const float cells = float(TerrainChunkCells);
			int low[3];
			int high[3];
			for (int axis = 0; axis < 3; ++axis)
			{
				const float at = local[axis];
				low[axis] = at <= 0.0f ? -1 : (at <= cells ? 0 : 1);
				high[axis] = at < 0.0f ? -1 : (at < cells ? 0 : 1);
			}
			for (int z = low[2]; z <= high[2]; ++z)
			{
				for (int y = low[1]; y <= high[1]; ++y)
				{
					for (int x = low[0]; x <= high[0]; ++x)
					{
						visit(glm::ivec3(x, y, z));
					}
				}
			}
		}

		// The finest and coarsest cells at a point, as multiples of the chunk's own (FieldSeams::
		// CellSizes); nothing there counts as the chunk's own size
		glm::vec2 GetCellSizes(const TerrainNeighbourhood& neighbourhood, const glm::vec3& local)
		{
			int finest = 1;
			int coarsest = -1;
			ForPlacesAt(local, [&](const glm::ivec3& offset)
				{
					const int level = neighbourhood.At(offset) == TerrainNeighbourhood::None ? 0 : neighbourhood.At(offset);
					finest = std::min(finest, level);
					coarsest = std::max(coarsest, level);
				});
			return glm::vec2(std::ldexp(1.0f, finest), std::ldexp(1.0f, coarsest));
		}

		// The colour of a level in the debug views
		glm::vec3 LevelColor(int level)
		{
			static const glm::vec3 colors[] = { glm::vec3(0.85f, 0.85f, 0.85f), glm::vec3(0.15f, 0.60f, 0.15f), glm::vec3(0.15f, 0.35f, 0.85f),
				glm::vec3(0.90f, 0.50f, 0.10f), glm::vec3(0.55f, 0.20f, 0.75f), glm::vec3(0.10f, 0.70f, 0.70f), glm::vec3(0.85f, 0.80f, 0.15f),
				glm::vec3(0.80f, 0.15f, 0.15f) };
			const int index = std::clamp(level - TerrainFinestLevel, 0, int(std::size(colors)) - 1);
			return colors[index];
		}

		// At most this many chunks a terrain in flight, as many as there are workers twice over, and
		// at most so many uploaded in a frame, so a burst of them never costs a frame much
		uint32_t GetMaxJobs(const JobSystem& jobs)
		{
			return std::max(jobs.GetWorkerCount() * 2, 2u);
		}
		constexpr uint32_t MaxUploadsPerFrame = 48;
		// A viewpoint moved this far, as a part of the finest level's chunk, chooses the chunks again
		constexpr float ReplanDistance = 0.125f;
		// A chunk's vertices are at whole parts of a metre this fine
		constexpr float PositionSteps = 4096.0f;

		TerrainLevelSettings GetLevelSettings(const SdfTerrainComponent& terrain)
		{
			TerrainLevelSettings settings;
			settings.ViewDistance = std::max(terrain.ViewDistance, 0.0f);
			settings.LevelDistance = terrain.LevelDistance;
			settings.FinestLevel = terrain.FinestLevel;
			return settings;
		}

		float GetReplanDistance(const SdfTerrainComponent& terrain)
		{
			return ReplanDistance * GetTerrainChunkSize(ClampLevel(terrain.FinestLevel));
		}

		uint32_t GetFirstShadowCascade(const SdfTerrainComponent& terrain, int level)
		{
			return static_cast<uint32_t>(std::max(level - ClampLevel(terrain.FinestLevel) - std::max(terrain.NearShadowLevels, 1) + 1, 0));
		}

		// Whether the surface may cross the samples a chunk's mesh is made from
		bool HasSurface(const SdfTerrainComponent& terrain, const TerrainChunkId& id)
		{
			const float cell = GetTerrainCellSize(id.Level);
			const glm::vec3 low = ChunkCorner(id) - 2.0f * cell;
			const glm::vec3 high = ChunkCorner(id) + GetTerrainChunkSize(id.Level) + cell;
			const TerrainValueRange range = GetTerrainValueRange(terrain, low, high);
			return !range.IsAllAir() && !range.IsAllSolid();
		}

		bool HasMoved(const std::vector<glm::vec3>& from, const std::vector<glm::vec3>& to, float distance)
		{
			if (from.size() != to.size())
			{
				return true;
			}
			for (size_t n = 0; n < from.size(); ++n)
			{
				if (glm::length(from[n] - to[n]) > distance)
				{
					return true;
				}
			}
			return false;
		}
	}

	namespace Detail
	{
		// What a terrain's chunks are being made from and what is in flight, shared with the jobs, so a
		// job that finishes after its terrain went writes into this and nothing else
		struct TerrainStreaming
		{
			// The values the chunks are being made from now, and a copy of them for the jobs
			uint64_t Shape = 0;
			std::shared_ptr<const SdfTerrainComponent> Snapshot;

			// The chunks chosen round the viewpoints, on a worker, one choice at a time
			struct Plan
			{
				uint64_t Shape = 0;
				std::vector<glm::vec3> Viewpoints;
				TerrainChunkTree Tree;
				std::vector<TerrainNeighbourhood> Neighbourhoods;
				std::vector<bool> Surface;
				double Milliseconds = 0.0;
			};
			struct Planning
			{
				std::mutex Lock;
				std::optional<Plan> Result;
			};
			std::shared_ptr<Planning> PlanInFlight;
			JobHandle PlanJob;
			// Whether the surface crosses a chunk, for the values now: asked once a chunk, by one choice
			// after another
			std::shared_ptr<std::unordered_map<uint64_t, bool>> SurfaceCache;
			// Where the viewpoints were when the chunks were last chosen for the values now
			std::optional<std::vector<glm::vec3>> PlannedFrom;

			// The tree drawn, and the next one and its chunks the surface crosses, nearest first
			TerrainChunkTree Drawn;
			uint32_t DrawnSkipped = 0;
			struct Wanted
			{
				TerrainChunkId Id;
				TerrainNeighbourhood Neighbourhood;
				float Distance = 0.0f;
			};
			std::optional<TerrainChunkTree> Next;
			std::vector<Wanted> NextChunks;
			uint32_t NextSkipped = 0;

			struct InFlight
			{
				JobHandle Job;
				uint64_t Shape = 0;
				uint64_t Neighbourhood = 0;
			};
			std::unordered_map<uint64_t, InFlight> Jobs;

			struct Done
			{
				TerrainChunkId Id;
				uint64_t Shape = 0;
				TerrainNeighbourhood Neighbourhood;
				std::shared_ptr<const MeshData> Mesh;
				TerrainChunkTimes Times;
			};
			std::mutex Lock;
			std::vector<Done> Finished;
			// Made for the next tree, not uploaded yet
			std::unordered_map<uint64_t, Done> Ready;
			// Where the jobs went, to cancel them when the terrain goes
			JobSystem* Workers = nullptr;
		};
	}

	float GetTerrainCellSize(int level)
	{
		return std::ldexp(TerrainCellSize, level);
	}

	float GetTerrainChunkSize(int level)
	{
		return std::ldexp(TerrainChunkSize, level);
	}

	bool TerrainNeighbourhood::HasSeams() const
	{
		return std::any_of(Levels.begin(), Levels.end(), [](int8_t level) { return level == -1 || level == 1; });
	}

	uint64_t TerrainNeighbourhood::Hash() const
	{
		uint64_t packed = 0;
		for (size_t n = 0; n < Levels.size(); ++n)
		{
			packed |= uint64_t(uint8_t(Levels[n] + 1) & 3u) << (2 * n);
		}
		return packed;
	}

	TerrainChunkTree SelectTerrainChunks(std::span<const glm::vec3> viewpoints, const TerrainLevelSettings& settings, const TerrainChunkTree* previous)
	{
		const int finest = ClampLevel(settings.FinestLevel);
		const float levelDistance = std::clamp(settings.LevelDistance, 2.0f, 16.0f);
		const float view = std::max(settings.ViewDistance, 0.0f);
		// A chunk of `level` splits where a viewpoint is nearer than this
		auto splitDistance = [&](int level) { return levelDistance * GetTerrainChunkSize(level - 1); };
		// The coarsest level drawn within the view distance
		int top = finest;
		while (top < TerrainCoarsestLevel && splitDistance(top + 1) <= view)
		{
			++top;
		}

		std::unordered_set<uint64_t> wasSplit;
		if (previous != nullptr)
		{
			for (const TerrainChunkId& id : previous->Split)
			{
				wasSplit.insert(PackId(id));
			}
		}

		// From the top level's chunks round each viewpoint down
		std::vector<TerrainChunkId> pending;
		std::unordered_set<uint64_t> roots;
		for (const glm::vec3& viewpoint : viewpoints)
		{
			const float size = GetTerrainChunkSize(top);
			const glm::ivec3 low(glm::floor((viewpoint - view) / size));
			const glm::ivec3 high(glm::floor((viewpoint + view) / size));
			for (int z = low.z; z <= high.z; ++z)
			{
				for (int y = low.y; y <= high.y; ++y)
				{
					for (int x = low.x; x <= high.x; ++x)
					{
						const TerrainChunkId id{ top, glm::ivec3(x, y, z) };
						if (roots.insert(PackId(id)).second)
						{
							pending.push_back(id);
						}
					}
				}
			}
		}
		std::unordered_set<uint64_t> leaves;
		std::unordered_set<uint64_t> split;
		std::vector<TerrainChunkId> leafList;
		while (!pending.empty())
		{
			const TerrainChunkId id = pending.back();
			pending.pop_back();
			const float distance = NearestViewpoint(viewpoints, id);
			if (distance > view)
			{
				continue;
			}
			const float limit = id.Level > finest ? splitDistance(id.Level) * (wasSplit.contains(PackId(id)) ? 1.0f + settings.Hysteresis : 1.0f) : 0.0f;
			if (distance < limit)
			{
				split.insert(PackId(id));
				for (int child = 0; child < 8; ++child)
				{
					pending.push_back({ id.Level - 1, ChildKey(id.Key, child) });
				}
				continue;
			}
			leaves.insert(PackId(id));
			leafList.push_back(id);
		}

		// Balanced: a chunk two or more levels coarser than one it touches is split, until none is
		std::vector<TerrainChunkId> check = leafList;
		while (!check.empty())
		{
			const TerrainChunkId id = check.back();
			check.pop_back();
			if (!leaves.contains(PackId(id)))
			{
				continue;
			}
			const float size = GetTerrainChunkSize(id.Level);
			for (int n = 0; n < 27; ++n)
			{
				const glm::ivec3 offset = NeighbourOffset(n);
				if (offset == glm::ivec3(0))
				{
					continue;
				}
				// The middle of the place of its size there, in whichever chunk has it
				const glm::vec3 middle = (glm::vec3(id.Key + offset) + 0.5f) * size;
				for (int level = id.Level + 2; level <= top; ++level)
				{
					const TerrainChunkId coarse{ level, glm::ivec3(glm::floor(middle / GetTerrainChunkSize(level))) };
					if (leaves.erase(PackId(coarse)) == 0)
					{
						continue;
					}
					split.insert(PackId(coarse));
					for (int child = 0; child < 8; ++child)
					{
						const TerrainChunkId finer{ coarse.Level - 1, ChildKey(coarse.Key, child) };
						leaves.insert(PackId(finer));
						leafList.push_back(finer);
						check.push_back(finer);
					}
					break;
				}
			}
		}

		TerrainChunkTree tree;
		for (const TerrainChunkId& id : leafList)
		{
			if (leaves.erase(PackId(id)) != 0)
			{
				tree.Leaves.push_back(id);
			}
		}
		std::sort(tree.Leaves.begin(), tree.Leaves.end(), IsLexicographicallyBefore);
		for (const uint64_t packed : split)
		{
			tree.Split.push_back(UnpackId(packed));
		}
		std::sort(tree.Split.begin(), tree.Split.end(), IsLexicographicallyBefore);
		return tree;
	}

	std::vector<TerrainNeighbourhood> GetTerrainNeighbourhoods(const TerrainChunkTree& tree)
	{
		std::unordered_set<uint64_t> leaves;
		for (const TerrainChunkId& id : tree.Leaves)
		{
			leaves.insert(PackId(id));
		}
		std::vector<TerrainNeighbourhood> neighbourhoods(tree.Leaves.size());
		for (size_t index = 0; index < tree.Leaves.size(); ++index)
		{
			const TerrainChunkId& id = tree.Leaves[index];
			for (int n = 0; n < 27; ++n)
			{
				const glm::ivec3 offset = NeighbourOffset(n);
				const glm::ivec3 key = id.Key + offset;
				int8_t level = TerrainNeighbourhood::None;
				if (offset == glm::ivec3(0) || leaves.contains(PackId(id.Level, key)))
				{
					level = 0;
				}
				for (int coarser = id.Level + 1; coarser <= TerrainCoarsestLevel && level == TerrainNeighbourhood::None; ++coarser)
				{
					if (leaves.contains(PackId(coarser, key >> (coarser - id.Level))))
					{
						level = 1;
					}
				}
				for (int child = 0; child < 8 && level == TerrainNeighbourhood::None && id.Level > TerrainFinestLevel; ++child)
				{
					if (leaves.contains(PackId(id.Level - 1, ChildKey(key, child))))
					{
						level = -1;
					}
				}
				neighbourhoods[index].At(offset) = level;
			}
		}
		return neighbourhoods;
	}

	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& terrain, const TerrainChunkId& id, const TerrainNeighbourhood& neighbourhood, TerrainChunkTimes* outTimes)
	{
		const auto start = std::chrono::steady_clock::now();
		const float cell = GetTerrainCellSize(id.Level);
		const glm::vec3 corner = ChunkCorner(id);
		// Two cells before the corner and one past the far side: the cells the chunk's quads join, and
		// the samples either side of their corners
		FieldGrid grid = SampleTerrainGrid(terrain, corner - 2.0f * cell, cell, glm::ivec3(TerrainChunkCells + 4));

		// Next to coarser chunks: their samples, three of their cells round this one, and this one's
		// samples where they touch theirs and between them their average
		const bool bCoarser = std::any_of(neighbourhood.Levels.begin(), neighbourhood.Levels.end(), [](int8_t level) { return level == 1; });
		FieldGrid coarse;
		if (bCoarser)
		{
			coarse = SampleTerrainGrid(terrain, corner - 6.0f * cell, 2.0f * cell, glm::ivec3(TerrainChunkCells / 2 + 7));
			for (int k = 0; k < grid.Size.z; ++k)
			{
				for (int j = 0; j < grid.Size.y; ++j)
				{
					for (int i = 0; i < grid.Size.x; ++i)
					{
						const glm::ivec3 local = glm::ivec3(i, j, k) - 2;
						int axes[3];
						int count = 0;
						for (int axis = 0; axis < 3; ++axis)
						{
							if ((local[axis] & 1) != 0)
							{
								axes[count++] = axis;
							}
						}
						bool bTouched = false;
						if (count > 0)
						{
							ForPlacesAt(glm::vec3(local), [&](const glm::ivec3& offset) { bTouched = bTouched || neighbourhood.At(offset) == 1; });
						}
						if (!bTouched)
						{
							continue;
						}
						// The coarser corners round it, in order of the axes it is between them on
						float corners[8];
						uint32_t material = 0;
						for (int n = 0; n < (1 << count); ++n)
						{
							glm::ivec3 at = local;
							for (int m = 0; m < count; ++m)
							{
								at[axes[m]] += ((n >> m) & 1) != 0 ? 1 : -1;
							}
							const glm::ivec3 index = (at + 6) / 2;
							corners[n] = coarse.GetValue(index.x, index.y, index.z);
							if (n == 0)
							{
								material = coarse.GetMaterial(index.x, index.y, index.z);
							}
						}
						const size_t index = grid.IndexOf(i, j, k);
						grid.Values[index] = InterpolateSeamSample(corners, 1 << count);
						grid.Materials[index] = material;
					}
				}
			}
		}
		const auto sampled = std::chrono::steady_clock::now();

		FieldMeshSettings settings;
		settings.QuadMin = glm::ivec3(2);
		settings.QuadMax = glm::ivec3(2 + TerrainChunkCells);
		for (const uint32_t material : terrain.SharpMaterials)
		{
			if (material < 256)
			{
				settings.SharpMaterials.resize(std::max<size_t>(settings.SharpMaterials.size(), material + 1), false);
				settings.SharpMaterials[material] = true;
			}
		}
		settings.Field = [&terrain](const float* x, const float* y, const float* z, size_t count, float* out) { SampleTerrainBatch(terrain, x, y, z, count, out); };
		// Next to another level, or nothing, the seams: what is round it by the neighbourhood, and of
		// the edges on its faces those it makes, as the finest chunks that have them, the last of them
		if (std::any_of(neighbourhood.Levels.begin(), neighbourhood.Levels.end(), [](int8_t level) { return level != 0; }))
		{
			settings.Seams.CellSizes = [&neighbourhood](const glm::vec3& at) { return GetCellSizes(neighbourhood, at - 2.0f); };
			settings.Seams.MakesEdge = [&neighbourhood](const glm::ivec3& from, int axis)
			{
				glm::vec3 middle = glm::vec3(from - 2);
				middle[axis] += 0.5f;
				bool bMakes = true;
				ForPlacesAt(middle, [&](const glm::ivec3& offset)
					{
						const int8_t level = neighbourhood.At(offset);
						const bool bLater = offset.x != 0 ? offset.x > 0 : (offset.y != 0 ? offset.y > 0 : offset.z > 0);
						bMakes = bMakes && level != -1 && !(level == 0 && bLater);
					});
				return bMakes;
			};
			settings.Seams.Coarser = bCoarser ? &coarse : nullptr;
		}
		FieldMeshStats stats;
		MeshData mesh = terrain.Mesher == ESdfTerrainMesher::MarchingCubes ? MakeMarchingFieldMesh(grid, settings, &stats) : MakeFieldMesh(grid, settings, &stats);

		// Around the chunk's corner, at whole 4096ths of a metre: a vertex two chunks share is at the
		// same place in both meshes before (the meshers work in the terrain's space), so at the same
		// 4096th, and from there to a corner within 4 km is exact, the same bits in both once the
		// corner is added back
		const glm::vec3 levelColor = LevelColor(id.Level);
		for (Vertex& vertex : mesh.Vertices)
		{
			vertex.Position = glm::round(vertex.Position * PositionSteps) * (1.0f / PositionSteps) - corner;
			const uint32_t material = static_cast<uint32_t>(vertex.Custom.y);
			switch (terrain.DebugView)
			{
			case ESdfTerrainDebugView::Levels:
				vertex.Color = levelColor;
				break;
			case ESdfTerrainDebugView::Seams:
				vertex.Color = vertex.Custom.w > 0.5f ? glm::vec3(1.0f, 0.1f, 0.8f) : levelColor * 0.35f;
				break;
			default:
				vertex.Color = material < terrain.MaterialColors.size() ? terrain.MaterialColors[material] : glm::vec3(1.0f);
				break;
			}
		}
		if (outTimes != nullptr)
		{
			outTimes->SampleMilliseconds = std::chrono::duration<double, std::milli>(sampled - start).count();
			outTimes->MeshMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sampled).count();
			outTimes->Triangles = static_cast<uint32_t>(mesh.Indices.size() / 3);
			outTimes->SeamTriangles = stats.SeamTriangles;
		}
		return mesh;
	}

	uint64_t HashSdfTerrainShape(const SdfTerrainComponent& terrain)
	{
		Hasher hasher;
		hasher.Value(terrain.Seed);
		hasher.Value(terrain.Mesher);
		hasher.Value(terrain.DebugView);
		hasher.Value(terrain.Layers.size());
		for (const SdfTerrainLayer& layer : terrain.Layers)
		{
			hasher.Value(layer.bEnabled);
			hasher.Value(layer.Kind);
			hasher.Value(layer.Combine);
			hasher.Value(layer.BlendRadius);
			hasher.Value(layer.Material);
			hasher.Value(layer.Height);
			hasher.Value(layer.Depth);
			hasher.Value(layer.Amplitude);
			hasher.Value(layer.Center);
			hasher.Value(layer.Radius);
			hasher.Value(layer.UndersideDepth);
			hasher.Value(layer.EdgeRoughness);
			hasher.Value(layer.Rounding);
			hasher.Value(layer.Openness);
			hasher.Value(layer.TerraceHeight);
			hasher.Value(layer.TerraceSharpness);
			hasher.Value(layer.Seed);
			hasher.Value(layer.Octaves);
			hasher.Value(layer.Frequency);
			hasher.Value(layer.Lacunarity);
			hasher.Value(layer.Gain);
			hasher.Value(layer.bRidged);
		}
		hasher.Value(terrain.SharpMaterials.size());
		for (const uint32_t material : terrain.SharpMaterials)
		{
			hasher.Value(material);
		}
		hasher.Value(terrain.MaterialColors.size());
		for (const glm::vec3& color : terrain.MaterialColors)
		{
			hasher.Value(color);
		}
		return hasher.Hash;
	}

	namespace
	{
		// A chunk as made: where, from which values, against what round it
		uint64_t VersionOf(const TerrainChunkId& id, uint64_t shape, const TerrainNeighbourhood& neighbourhood)
		{
			Hasher hasher;
			hasher.Value(PackId(id));
			hasher.Value(shape);
			hasher.Value(neighbourhood.Hash());
			return hasher.Hash;
		}

		uint64_t VersionOf(const TerrainChunk& chunk)
		{
			return VersionOf({ chunk.Level, chunk.Key }, chunk.Shape, chunk.Neighbourhood);
		}

		// The chunks chosen on a worker, and whether the surface crosses each
		void SchedulePlan(Detail::TerrainStreaming& streaming, const SdfTerrainComponent& terrain, std::vector<glm::vec3> viewpoints, JobSystem& jobs)
		{
			auto planning = std::make_shared<Detail::TerrainStreaming::Planning>();
			streaming.PlanInFlight = planning;
			std::shared_ptr<const SdfTerrainComponent> snapshot = streaming.Snapshot;
			std::shared_ptr<std::unordered_map<uint64_t, bool>> cache = streaming.SurfaceCache;
			const TerrainLevelSettings settings = GetLevelSettings(terrain);
			TerrainChunkTree previous;
			previous.Split = streaming.Drawn.Split;
			const uint64_t shape = streaming.Shape;
			streaming.PlanJob = jobs.Schedule(
				[planning, snapshot, cache, settings, previous = std::move(previous), viewpoints = std::move(viewpoints), shape]
				{
					const auto start = std::chrono::steady_clock::now();
					Detail::TerrainStreaming::Plan plan;
					plan.Shape = shape;
					plan.Viewpoints = viewpoints;
					plan.Tree = SelectTerrainChunks(viewpoints, settings, &previous);
					plan.Neighbourhoods = GetTerrainNeighbourhoods(plan.Tree);
					plan.Surface.resize(plan.Tree.Leaves.size());
					for (size_t index = 0; index < plan.Tree.Leaves.size(); ++index)
					{
						const TerrainChunkId& id = plan.Tree.Leaves[index];
						const uint64_t packed = PackId(id);
						const auto found = cache->find(packed);
						const bool bSurface = found != cache->end() ? found->second : HasSurface(*snapshot, id);
						cache->emplace(packed, bSurface);
						plan.Surface[index] = bSurface;
					}
					plan.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
					std::lock_guard lock(planning->Lock);
					planning->Result = std::move(plan);
				},
				EJobPriority::Normal);
		}

		// A choice made: the next tree, unless its chunks are those drawn
		void TakePlan(TerrainChunksComponent& chunks, Detail::TerrainStreaming& streaming, Detail::TerrainStreaming::Plan& plan)
		{
			chunks.Stats.LastPlanMilliseconds = plan.Milliseconds;
			streaming.PlannedFrom = plan.Viewpoints;
			std::vector<Detail::TerrainStreaming::Wanted> wanted;
			uint32_t skipped = 0;
			for (size_t index = 0; index < plan.Tree.Leaves.size(); ++index)
			{
				if (!plan.Surface[index])
				{
					++skipped;
					continue;
				}
				wanted.push_back({ plan.Tree.Leaves[index], plan.Neighbourhoods[index], NearestViewpoint(plan.Viewpoints, plan.Tree.Leaves[index]) });
			}
			std::sort(wanted.begin(), wanted.end(), [](const Detail::TerrainStreaming::Wanted& a, const Detail::TerrainStreaming::Wanted& b)
				{
					return a.Distance != b.Distance ? a.Distance < b.Distance : IsLexicographicallyBefore(a.Id, b.Id);
				});

			std::unordered_set<uint64_t> drawn;
			for (const TerrainChunk& chunk : chunks.Chunks)
			{
				if (chunk.bDrawn)
				{
					drawn.insert(VersionOf(chunk));
				}
			}
			bool bSame = drawn.size() == wanted.size() && streaming.Drawn.Leaves == plan.Tree.Leaves;
			for (size_t n = 0; n < wanted.size() && bSame; ++n)
			{
				bSame = drawn.contains(VersionOf(wanted[n].Id, streaming.Shape, wanted[n].Neighbourhood));
			}
			if (bSame)
			{
				streaming.Drawn = std::move(plan.Tree);
				streaming.DrawnSkipped = skipped;
				return;
			}
			streaming.Next = std::move(plan.Tree);
			streaming.NextChunks = std::move(wanted);
			streaming.NextSkipped = skipped;
		}

		void UpdateTerrain(Registry& world, Entity entity, const SdfTerrainComponent& terrain, std::span<const glm::vec3> worldViewpoints, JobSystem& jobs)
		{
			if (!world.Has<TerrainChunksComponent>(entity))
			{
				world.Add<TerrainChunksComponent>(entity, TerrainChunksComponent{});
			}
			TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(entity);
			if (!chunks.Streaming)
			{
				chunks.Streaming = std::make_shared<Detail::TerrainStreaming>();
			}
			Detail::TerrainStreaming& streaming = *chunks.Streaming;
			streaming.Workers = &jobs;

			// New values: a copy for the jobs, what was known of the old ones forgotten, their jobs
			// cancelled and what they made let go of; the chunks drawn stay until the next ones are in
			const uint64_t shape = HashSdfTerrainShape(terrain);
			if (shape != streaming.Shape || !streaming.Snapshot)
			{
				streaming.Shape = shape;
				streaming.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
				streaming.SurfaceCache = std::make_shared<std::unordered_map<uint64_t, bool>>();
				streaming.PlannedFrom.reset();
				streaming.Next.reset();
				streaming.NextChunks.clear();
				for (auto& [key, job] : streaming.Jobs)
				{
					jobs.Cancel(job.Job);
				}
				streaming.Jobs.clear();
				streaming.Ready.clear();
				std::erase_if(chunks.Chunks, [](const TerrainChunk& chunk) { return !chunk.bDrawn; });
			}

			// The viewpoints in the terrain's own space
			const glm::mat4 toTerrain = world.Has<TransformComponent>(entity) ? glm::inverse(world.Get<TransformComponent>(entity).ToMatrix()) : glm::mat4(1.0f);
			std::vector<glm::vec3> viewpoints;
			for (const glm::vec3& viewpoint : worldViewpoints)
			{
				viewpoints.push_back(glm::vec3(toTerrain * glm::vec4(viewpoint, 1.0f)));
			}

			if (streaming.PlanInFlight)
			{
				std::optional<Detail::TerrainStreaming::Plan> plan;
				{
					std::lock_guard lock(streaming.PlanInFlight->Lock);
					plan.swap(streaming.PlanInFlight->Result);
				}
				if (plan)
				{
					streaming.PlanInFlight.reset();
					if (plan->Shape == streaming.Shape)
					{
						TakePlan(chunks, streaming, *plan);
					}
				}
			}

			// Nothing being chosen or made: chosen again once a viewpoint has moved, or for new values
			if (!streaming.PlanInFlight && !streaming.Next && !viewpoints.empty() &&
				(!streaming.PlannedFrom || HasMoved(*streaming.PlannedFrom, viewpoints, GetReplanDistance(terrain))))
			{
				SchedulePlan(streaming, terrain, viewpoints, jobs);
			}

			// What the workers made, kept if the next tree still wants it
			std::vector<Detail::TerrainStreaming::Done> finished;
			{
				std::lock_guard lock(streaming.Lock);
				finished.swap(streaming.Finished);
			}
			std::unordered_map<uint64_t, uint64_t> wantedVersions;
			for (const Detail::TerrainStreaming::Wanted& chunk : streaming.NextChunks)
			{
				wantedVersions.emplace(PackId(chunk.Id), VersionOf(chunk.Id, streaming.Shape, chunk.Neighbourhood));
			}
			for (Detail::TerrainStreaming::Done& done : finished)
			{
				const uint64_t packed = PackId(done.Id);
				const auto job = streaming.Jobs.find(packed);
				if (job != streaming.Jobs.end() && job->second.Shape == done.Shape && job->second.Neighbourhood == done.Neighbourhood.Hash())
				{
					streaming.Jobs.erase(job);
				}
				const auto wanted = wantedVersions.find(packed);
				if (wanted == wantedVersions.end() || wanted->second != VersionOf(done.Id, done.Shape, done.Neighbourhood))
				{
					++chunks.Stats.Dropped;
					continue;
				}
				++chunks.Stats.Made;
				chunks.Stats.LastSampleMilliseconds = done.Times.SampleMilliseconds;
				chunks.Stats.LastMeshMilliseconds = done.Times.MeshMilliseconds;
				streaming.Ready[packed] = std::move(done);
			}

			if (streaming.Next)
			{
				std::unordered_set<uint64_t> present;
				for (const TerrainChunk& chunk : chunks.Chunks)
				{
					present.insert(VersionOf(chunk));
				}

				uint32_t uploads = 0;
				bool bAllPresent = true;
				const uint32_t maxJobs = GetMaxJobs(jobs);
				for (const Detail::TerrainStreaming::Wanted& chunk : streaming.NextChunks)
				{
					const uint64_t packed = PackId(chunk.Id);
					const uint64_t version = wantedVersions[packed];
					if (present.contains(version))
					{
						continue;
					}
					bAllPresent = false;

					// Made: uploaded, nearest first, a few a frame, in the component but not drawn yet
					const auto ready = streaming.Ready.find(packed);
					if (ready != streaming.Ready.end())
					{
						if (uploads >= MaxUploadsPerFrame)
						{
							continue;
						}
						TerrainChunk made;
						made.Level = chunk.Id.Level;
						made.Key = chunk.Id.Key;
						made.Origin = ChunkCorner(chunk.Id);
						made.Shape = streaming.Shape;
						made.Neighbourhood = chunk.Neighbourhood;
						if (ready->second.Mesh && !ready->second.Mesh->Indices.empty())
						{
							SetRuntimeMesh(made.Mesh, std::move(ready->second.Mesh));
						}
						chunks.Chunks.push_back(std::move(made));
						present.insert(version);
						streaming.Ready.erase(ready);
						++uploads;
						continue;
					}

					// Missing: to the workers, nearest first, a few at a time
					const auto job = streaming.Jobs.find(packed);
					if (job != streaming.Jobs.end())
					{
						if (job->second.Shape == streaming.Shape && job->second.Neighbourhood == chunk.Neighbourhood.Hash())
						{
							continue;
						}
						jobs.Cancel(job->second.Job);
						streaming.Jobs.erase(job);
					}
					if (streaming.Jobs.size() >= maxJobs)
					{
						continue;
					}
					std::shared_ptr<Detail::TerrainStreaming> shared = chunks.Streaming;
					std::shared_ptr<const SdfTerrainComponent> snapshot = streaming.Snapshot;
					const TerrainChunkId id = chunk.Id;
					const TerrainNeighbourhood neighbourhood = chunk.Neighbourhood;
					const uint64_t jobShape = streaming.Shape;
					JobHandle handle = jobs.Schedule(
						[shared, snapshot, id, neighbourhood, jobShape]
						{
							Detail::TerrainStreaming::Done done;
							done.Id = id;
							done.Shape = jobShape;
							done.Neighbourhood = neighbourhood;
							done.Mesh = std::make_shared<const MeshData>(MakeTerrainChunkMesh(*snapshot, id, neighbourhood, &done.Times));
							std::lock_guard lock(shared->Lock);
							shared->Finished.push_back(std::move(done));
						},
						EJobPriority::Normal);
					streaming.Jobs.emplace(packed, Detail::TerrainStreaming::InFlight{ handle, jobShape, neighbourhood.Hash() });
				}

				// Jobs for chunks the next tree doesn't want, cancelled if they haven't started
				for (auto it = streaming.Jobs.begin(); it != streaming.Jobs.end();)
				{
					if (!wantedVersions.contains(it->first) && jobs.Cancel(it->second.Job))
					{
						it = streaming.Jobs.erase(it);
						continue;
					}
					++it;
				}

				// All in: drawn at once in place of those drawn, so seams always meet their neighbours
				if (bAllPresent)
				{
					std::unordered_set<uint64_t> next;
					for (const auto& [packed, version] : wantedVersions)
					{
						next.insert(version);
					}
					std::erase_if(chunks.Chunks, [&](const TerrainChunk& chunk) { return !next.contains(VersionOf(chunk)); });
					for (TerrainChunk& chunk : chunks.Chunks)
					{
						chunk.bDrawn = true;
					}
					streaming.Drawn = std::move(*streaming.Next);
					streaming.DrawnSkipped = streaming.NextSkipped;
					streaming.Next.reset();
					streaming.NextChunks.clear();
					streaming.Ready.clear();
					++chunks.Stats.Swaps;
				}
			}

			TerrainChunksStats& stats = chunks.Stats;
			stats.Resident = 0;
			stats.Triangles = 0;
			stats.WithSeams = 0;
			stats.ByLevel.fill(0);
			for (TerrainChunk& chunk : chunks.Chunks)
			{
				chunk.FirstShadowCascade = GetFirstShadowCascade(terrain, chunk.Level);
				if (!chunk.bDrawn)
				{
					continue;
				}
				++stats.Resident;
				stats.Triangles += chunk.Mesh.Mesh ? chunk.Mesh.Mesh->Indices.size() / 3 : 0;
				stats.WithSeams += chunk.Neighbourhood.HasSeams() ? 1u : 0u;
				++stats.ByLevel[size_t(std::clamp(chunk.Level - TerrainFinestLevel, 0, int(stats.ByLevel.size()) - 1))];
			}
			stats.Wanted = streaming.Next ? static_cast<uint32_t>(streaming.NextChunks.size()) : stats.Resident;
			stats.Skipped = streaming.Next ? streaming.NextSkipped : streaming.DrawnSkipped;
			stats.Pending = static_cast<uint32_t>(streaming.Jobs.size());
			stats.bSettled = !streaming.Next && !streaming.PlanInFlight && streaming.PlannedFrom && !HasMoved(*streaming.PlannedFrom, viewpoints, GetReplanDistance(terrain));
		}
	}

	void UpdateTerrainChunks(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs)
	{
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		for (const Entity entity : terrains)
		{
			UpdateTerrain(world, entity, world.Get<SdfTerrainComponent>(entity), viewpoints, jobs);
		}
	}

	void RemoveTerrainChunks(Registry& world, Entity entity)
	{
		if (!world.Has<TerrainChunksComponent>(entity))
		{
			return;
		}
		// What hasn't started never runs; what runs finishes into the state it shares, then goes
		if (const std::shared_ptr<Detail::TerrainStreaming>& streaming = world.Get<TerrainChunksComponent>(entity).Streaming;
			streaming && streaming->Workers != nullptr)
		{
			for (auto& [key, job] : streaming->Jobs)
			{
				streaming->Workers->Cancel(job.Job);
			}
			streaming->Workers->Cancel(streaming->PlanJob);
		}
		world.Remove<TerrainChunksComponent>(entity);
	}

	std::vector<InspectedValue> DescribeTerrainChunks(const TerrainChunksComponent& component)
	{
		const TerrainChunksStats& stats = component.Stats;
		std::string byLevel;
		for (size_t index = 0; index < stats.ByLevel.size(); ++index)
		{
			if (stats.ByLevel[index] > 0)
			{
				byLevel += std::format("{}{}: {}", byLevel.empty() ? "" : ", ", int(index) + TerrainFinestLevel, stats.ByLevel[index]);
			}
		}
		return {
			{ "Chunks", std::format("{} drawn, {} next, {} being made", stats.Resident, stats.Wanted, stats.Pending) },
			{ "By Level", byLevel.empty() ? std::string("none") : byLevel },
			{ "Seams", std::format("{} next to another level", stats.WithSeams) },
			{ "Skipped", std::format("{} all air or all solid", stats.Skipped) },
			{ "Triangles", std::to_string(stats.Triangles) },
			{ "Made", std::format("{}, {} let go of; drawn anew {} times", stats.Made, stats.Dropped, stats.Swaps) },
			{ "Last Chunk", std::format("sampled in {:.2f} ms, meshed in {:.2f} ms", stats.LastSampleMilliseconds, stats.LastMeshMilliseconds) },
			{ "Last Choice", std::format("{:.2f} ms", stats.LastPlanMilliseconds) },
		};
	}

	std::vector<InspectedValue> DescribeTerrainCollision(const TerrainCollisionComponent& component)
	{
		const TerrainCollisionStats& stats = component.Stats;
		return {
			{ "Chunks", std::format("{} with bodies, {} wanted, {} being made", stats.Resident, stats.Wanted, stats.Pending) },
			{ "Skipped", std::format("{} all air or all solid", stats.Skipped) },
			{ "Triangles", std::to_string(stats.Triangles) },
			{ "Bodies Ahead", std::format("{} without collision yet, {} faster than it can follow", stats.Uncovered, stats.Outrunning) },
			{ "Made", std::format("{}, {} from chunks drawn", stats.Made, stats.Shared) },
			{ "Worst Chunk", std::format("{:.0f} ms from asked for to its body", stats.WorstSeconds * 1000.0) },
		};
	}
}

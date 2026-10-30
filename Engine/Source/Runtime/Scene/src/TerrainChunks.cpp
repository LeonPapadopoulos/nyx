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
		// New edits' chunks this many levels past the finest drawn go live with collision; those past
		// them follow when they are made
		constexpr int NearEditLevels = 1;
		// The most edits after an undone one the chunks are chosen again for on the main thread
		constexpr size_t MaxEditsChosenHere = 64;
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
			// The values the chunks are being made from now, their edits, and a copy of them for the jobs
			uint64_t Shape = 0;
			uint64_t Edits = 0;
			std::shared_ptr<const SdfTerrainComponent> Snapshot;

			// The chunks chosen round the viewpoints, on a worker, one choice at a time, from the copy of
			// the values then, with the edits reaching each
			struct Plan
			{
				uint64_t Shape = 0;
				uint64_t Edits = 0;
				std::shared_ptr<const SdfTerrainComponent> Snapshot;
				std::vector<glm::vec3> Viewpoints;
				TerrainChunkTree Tree;
				std::vector<TerrainNeighbourhood> Neighbourhoods;
				std::vector<bool> Surface;
				std::vector<uint64_t> ChunkEdits;
				double Milliseconds = 0.0;
			};
			struct Planning
			{
				std::mutex Lock;
				std::optional<Plan> Result;
			};
			std::shared_ptr<Planning> PlanInFlight;
			JobHandle PlanJob;
			// What is known of each chunk's values, for the values now: asked once a chunk and carried
			// across edits, by one choice after another
			std::shared_ptr<TerrainChunkFields> Fields;
			// The hashes of the edits' runs (HashTerrainEditRuns()), and which chunks drawn are still
			// as they would be made now, by one choice after another
			std::shared_ptr<const std::vector<uint64_t>> EditRuns;
			std::shared_ptr<TerrainChunkKeeps> Keeps;
			// Where the edits are, for the copy; and how many of the edits the chunks' fields have seen
			// are still the same, for the next choice to rewind them to (none: all)
			std::shared_ptr<const TerrainEditIndex> EditIndex;
			size_t RewindTo = std::numeric_limits<size_t>::max();
			// Where the viewpoints were when the chunks were last chosen for the values now
			std::optional<std::vector<glm::vec3>> PlannedFrom;
			// Where they were when the tree drawn or next was chosen, whatever the values, and what is
			// round each of the trees' leaves
			std::optional<std::vector<glm::vec3>> ChosenFrom;
			std::vector<TerrainNeighbourhood> DrawnNeighbourhoods;
			std::vector<TerrainNeighbourhood> NextNeighbourhoods;

			// The tree drawn and the edits it was made with, and the next one, its chunks the surface
			// crosses, nearest first, and what they are made from
			TerrainChunkTree Drawn;
			uint64_t DrawnEdits = 0;
			uint32_t DrawnSkipped = 0;
			struct Wanted
			{
				TerrainChunkId Id;
				uint64_t Edits = 0;
				TerrainNeighbourhood Neighbourhood;
				float Distance = 0.0f;
			};
			std::optional<TerrainChunkTree> Next;
			std::vector<Wanted> NextChunks;
			uint32_t NextSkipped = 0;
			uint64_t NextEdits = 0;
			std::shared_ptr<const SdfTerrainComponent> NextSnapshot;
			// New edits on the tree drawn, its near chunks drawn with them already
			bool bNextNearDrawn = false;

			struct InFlight
			{
				JobHandle Job;
				uint64_t Shape = 0;
				uint64_t Edits = 0;
				uint64_t Neighbourhood = 0;
			};
			std::unordered_map<uint64_t, InFlight> Jobs;

			struct Done
			{
				TerrainChunkId Id;
				uint64_t Shape = 0;
				uint64_t Edits = 0;
				TerrainNeighbourhood Neighbourhood;
				std::shared_ptr<const MeshData> Mesh;
				TerrainChunkTimes Times;
				std::shared_ptr<const TerrainChunkSamples> Samples;
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

	namespace
	{
		// A grid's bricks near the surface into `out`: those of 4 x 4 x 4 cells (their samples, the
		// faces between them shared) the surface crosses (inside and outside samples both), and those
		// next to them, even at a corner, and those overlapping `alsoNear` (another grid's, a cell
		// round them)
		void AddNearSurfaceBricks(const FieldGrid& grid, const std::vector<TerrainChunkSamples::Brick>* alsoNear, std::vector<TerrainChunkSamples::Brick>& out)
		{
			constexpr int Cells = 4;
			const glm::ivec3 count = glm::max((grid.Size - 1 + (Cells - 1)) / Cells, glm::ivec3(1));
			struct Read
			{
				TerrainChunkSamples::Brick Brick;
				bool bCrossed = false;
			};
			std::vector<Read> bricks(size_t(count.x) * size_t(count.y) * size_t(count.z));
			auto indexOf = [&](const glm::ivec3& b) { return size_t(b.x) + size_t(count.x) * (size_t(b.y) + size_t(count.y) * size_t(b.z)); };
			for (int bz = 0; bz < count.z; ++bz)
			{
				for (int by = 0; by < count.y; ++by)
				{
					for (int bx = 0; bx < count.x; ++bx)
					{
						const glm::ivec3 first = glm::ivec3(bx, by, bz) * Cells;
						const glm::ivec3 last = glm::min(first + Cells, grid.Size - 1);
						Read& read = bricks[indexOf({ bx, by, bz })];
						read.Brick.Min = grid.Origin + glm::vec3(first) * grid.CellSize;
						read.Brick.Max = grid.Origin + glm::vec3(last) * grid.CellSize;
						read.Brick.Lowest = std::numeric_limits<float>::max();
						read.Brick.Highest = -std::numeric_limits<float>::max();
						bool bInside = false;
						bool bOutside = false;
						for (int k = first.z; k <= last.z; ++k)
						{
							for (int j = first.y; j <= last.y; ++j)
							{
								for (int i = first.x; i <= last.x; ++i)
								{
									const float value = grid.GetValue(i, j, k);
									read.Brick.Lowest = std::min(read.Brick.Lowest, value);
									read.Brick.Highest = std::max(read.Brick.Highest, value);
									// As the meshers tell inside: below 0
									const bool bIn = value < 0.0f;
									bInside = bInside || bIn;
									bOutside = bOutside || !bIn;
								}
							}
						}
						read.bCrossed = bInside && bOutside;
						read.Brick.bCrossed = read.bCrossed;
					}
				}
			}
			for (int bz = 0; bz < count.z; ++bz)
			{
				for (int by = 0; by < count.y; ++by)
				{
					for (int bx = 0; bx < count.x; ++bx)
					{
						const Read& read = bricks[indexOf({ bx, by, bz })];
						bool bNear = false;
						for (int dz = -1; dz <= 1 && !bNear; ++dz)
						{
							for (int dy = -1; dy <= 1 && !bNear; ++dy)
							{
								for (int dx = -1; dx <= 1 && !bNear; ++dx)
								{
									const glm::ivec3 other(bx + dx, by + dy, bz + dz);
									bNear = glm::all(glm::greaterThanEqual(other, glm::ivec3(0))) && glm::all(glm::lessThan(other, count)) && bricks[indexOf(other)].bCrossed;
								}
							}
						}
						if (!bNear && alsoNear != nullptr)
						{
							bNear = std::any_of(alsoNear->begin(), alsoNear->end(),
								[&](const TerrainChunkSamples::Brick& near)
								{
									return glm::all(glm::lessThanEqual(read.Brick.Min, near.Max + grid.CellSize)) &&
										   glm::all(glm::lessThanEqual(near.Min - grid.CellSize, read.Brick.Max));
								});
						}
						if (bNear)
						{
							out.push_back(read.Brick);
						}
					}
				}
			}
		}
	}

	bool CanTerrainEditChangeChunk(const TerrainChunkSamples& samples, const SdfTerrainEdit& edit)
	{
		if (CanTerrainEditTurnSign(edit, samples.GridMin, samples.GridMax))
		{
			return true;
		}
		// Up a sky's ray, from half a cell off a brick the surface crosses: a point there is at least
		// as high above where the ray began as it is away from it sideways, and within the reach
		glm::vec3 signMin;
		glm::vec3 signMax;
		if (samples.SkyReach > 0.0f && GetTerrainEditSignBounds(edit, signMin, signMax))
		{
			for (const TerrainChunkSamples::Brick& brick : samples.NearSurface)
			{
				if (!brick.bCrossed)
				{
					continue;
				}
				const glm::vec3 low = brick.Min - 0.5f * samples.CellSize;
				const glm::vec3 high = brick.Max + 0.5f * samples.CellSize;
				const glm::vec3 apart = glm::max(glm::max(low - signMax, signMin - high), glm::vec3(0.0f));
				const float sideways = std::sqrt(apart.x * apart.x + apart.z * apart.z);
				if (signMax.y - low.y + 1e-3f >= sideways && glm::length(apart) <= samples.SkyReach + 1e-3f)
				{
					return true;
				}
			}
		}
		if (samples.NearSurface.empty() || !CanTerrainEditChangeValues(edit, samples.NearMin, samples.NearMax, samples.NearLowest, samples.NearHighest))
		{
			return false;
		}
		for (const TerrainChunkSamples::Brick& brick : samples.NearSurface)
		{
			if (CanTerrainEditChangeValues(edit, brick.Min, brick.Max, brick.Lowest, brick.Highest))
			{
				return true;
			}
		}
		return false;
	}

	uint64_t TerrainChunkKeeps::Get(const SdfTerrainComponent& terrain, const std::vector<uint64_t>& runs, const TerrainChunkId& id, uint64_t madeEdits,
		const TerrainChunkSamples* samples, uint64_t now)
	{
		// Made with the first edits of these, the same ones
		const size_t count = terrain.Edits.size();
		if (samples == nullptr || samples->Edits > count || runs.size() != count + 1 || runs[samples->Edits] != samples->EditsHash)
		{
			return now;
		}
		Kept& kept = Known[PackId(id)];
		if (kept.Edits != madeEdits || kept.Seen < samples->Edits || kept.Seen > count || runs[kept.Seen] != kept.SeenHash)
		{
			kept = Kept{ madeEdits, samples->Edits, samples->EditsHash, false };
		}
		for (; kept.Seen < count && !kept.bChanged; ++kept.Seen)
		{
			kept.bChanged = CanTerrainEditChangeChunk(*samples, terrain.Edits[kept.Seen]);
		}
		kept.Seen = count;
		kept.SeenHash = runs[count];
		return kept.bChanged ? now : madeEdits;
	}

	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& allEdits, const TerrainChunkId& id, const TerrainNeighbourhood& neighbourhood, TerrainChunkTimes* outTimes,
		TerrainChunkSamples* outSamples, const FieldRunParts& runParts)
	{
		const auto start = std::chrono::steady_clock::now();
		// With only the edits that can change what it samples: the same bits, far fewer edits to pass
		std::optional<SdfTerrainComponent> reached;
		if (!allEdits.Edits.empty())
		{
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainChunkReach(id, low, high);
			reached = allEdits;
			KeepTerrainEditsReaching(*reached, low, high);
		}
		const SdfTerrainComponent& terrain = reached ? *reached : allEdits;
		const float cell = GetTerrainCellSize(id.Level);
		const glm::vec3 corner = ChunkCorner(id);
		// Two cells before the corner and one past the far side: the cells the chunk's quads join, and
		// the samples either side of their corners
		FieldGrid grid = SampleTerrainGrid(terrain, corner - 2.0f * cell, cell, glm::ivec3(TerrainChunkCells + 4), runParts);

		// Next to coarser chunks: their samples, three of their cells round this one, and this one's
		// samples where they touch theirs and between them their average
		const bool bCoarser = std::any_of(neighbourhood.Levels.begin(), neighbourhood.Levels.end(), [](int8_t level) { return level == 1; });
		FieldGrid coarse;
		if (bCoarser)
		{
			coarse = SampleTerrainGrid(terrain, corner - 6.0f * cell, 2.0f * cell, glm::ivec3(TerrainChunkCells / 2 + 7), runParts);
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
		// Where the mesher asks the field itself in its grids, a cell round them (round sharp vertices'
		// crossings), and what it got: what its mesh read there; past them the sky's rays, which read
		// only whether it is inside
		std::vector<glm::vec4> asked;
		glm::vec3 askedMin = grid.Origin - cell;
		glm::vec3 askedMax = grid.Origin + glm::vec3(grid.Size - 1) * cell + cell;
		if (bCoarser)
		{
			askedMin = glm::min(askedMin, coarse.Origin - cell);
			askedMax = glm::max(askedMax, coarse.Origin + glm::vec3(coarse.Size - 1) * coarse.CellSize + cell);
		}
		const bool bRecordAsked = outSamples != nullptr;
		// Its sky only if asked for
		if (!terrain.bSkyShading)
		{
			settings.SkyReach = 0.0f;
		}
		// The sky's parts ask from other threads
		std::mutex askedLock;
		settings.RunParts = runParts;
		settings.Field = [&terrain, &asked, &askedLock, askedMin, askedMax, bRecordAsked](const float* x, const float* y, const float* z, size_t count, float* out)
		{
			SampleTerrainBatch(terrain, x, y, z, count, out);
			for (size_t i = 0; i < count && bRecordAsked; ++i)
			{
				const glm::vec3 point(x[i], y[i], z[i]);
				if (glm::all(glm::greaterThanEqual(point, askedMin)) && glm::all(glm::lessThanEqual(point, askedMax)))
				{
					std::lock_guard lock(askedLock);
					asked.push_back(glm::vec4(point, out[i]));
				}
			}
		};
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

		// What it read, the coarser grid's bricks over the finer one's near the surface too
		if (outSamples != nullptr)
		{
			TerrainChunkSamples& read = *outSamples;
			read = {};
			AddNearSurfaceBricks(grid, nullptr, read.NearSurface);
			if (bCoarser)
			{
				const std::vector<TerrainChunkSamples::Brick> fine = read.NearSurface;
				AddNearSurfaceBricks(coarse, &fine, read.NearSurface);
			}
			// The points asked, in bricks of the finer grid's size: each the box of its points and
			// the lowest and highest the field was at them
			std::unordered_map<uint64_t, TerrainChunkSamples::Brick> askedBricks;
			for (const glm::vec4& point : asked)
			{
				const glm::ivec3 at(glm::floor((glm::vec3(point) - grid.Origin) / (4.0f * cell)));
				auto [it, bAdded] = askedBricks.try_emplace(PackId({ 0, at }));
				TerrainChunkSamples::Brick& brick = it->second;
				if (bAdded)
				{
					brick.Min = brick.Max = glm::vec3(point);
					brick.Lowest = brick.Highest = point.w;
					brick.bAsked = true;
					continue;
				}
				brick.Min = glm::min(brick.Min, glm::vec3(point));
				brick.Max = glm::max(brick.Max, glm::vec3(point));
				brick.Lowest = std::min(brick.Lowest, point.w);
				brick.Highest = std::max(brick.Highest, point.w);
			}
			for (const auto& [key, brick] : askedBricks)
			{
				read.NearSurface.push_back(brick);
			}
			read.CellSize = cell;
			read.NearMin = glm::vec3(std::numeric_limits<float>::max());
			read.NearMax = glm::vec3(-std::numeric_limits<float>::max());
			read.NearLowest = std::numeric_limits<float>::max();
			read.NearHighest = -std::numeric_limits<float>::max();
			for (const TerrainChunkSamples::Brick& brick : read.NearSurface)
			{
				read.NearMin = glm::min(read.NearMin, brick.Min);
				read.NearMax = glm::max(read.NearMax, brick.Max);
				read.NearLowest = std::min(read.NearLowest, brick.Lowest);
				read.NearHighest = std::max(read.NearHighest, brick.Highest);
			}
			// The grids' box: the finer one's, and the coarser one's round it
			read.GridMin = grid.Origin;
			read.GridMax = grid.Origin + glm::vec3(grid.Size - 1) * grid.CellSize;
			if (bCoarser)
			{
				read.GridMin = glm::min(read.GridMin, coarse.Origin);
				read.GridMax = glm::max(read.GridMax, coarse.Origin + glm::vec3(coarse.Size - 1) * coarse.CellSize);
			}
			read.SkyReach = settings.SkyReach;
			std::vector<uint64_t> runs;
			HashTerrainEditRuns(allEdits, runs);
			read.Edits = allEdits.Edits.size();
			read.EditsHash = runs.back();
		}

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
		hasher.Value(terrain.bSkyShading);
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
			hasher.Value(layer.Position);
			hasher.Value(layer.HalfSize);
			hasher.Value(layer.End);
			hasher.Value(layer.Rotation);
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

	void GetTerrainChunkReach(const TerrainChunkId& id, glm::vec3& outMin, glm::vec3& outMax)
	{
		// The coarser grid's six cells before the corner and six past the far side, which hold the
		// finer grid's and every cell the mesher asks the field in; and the sky's rays, from half a
		// cell off the vertices (within two cells of the chunk) up to SkyReach, all at least 45
		// degrees up, so as far up and at most 0.71 of it sideways, never down
		const float cell = GetTerrainCellSize(id.Level);
		const float sky = FieldMeshSettings{}.SkyReach;
		const float side = std::max(6.0f * cell, 2.5f * cell + 0.75f * sky);
		const float up = std::max(6.0f * cell, 2.5f * cell + sky);
		const float size = GetTerrainChunkSize(id.Level);
		outMin = ChunkCorner(id) - glm::vec3(side, 6.0f * cell, side);
		outMax = ChunkCorner(id) + glm::vec3(size + side, size + up, size + side);
	}

	TerrainChunkField BeginTerrainChunkField(const SdfTerrainComponent& terrain, const TerrainChunkId& id)
	{
		// The surface over the samples the mesh is made from: two cells before the corner, one past
		// the far side
		const float cell = GetTerrainCellSize(id.Level);
		TerrainChunkField field;
		field.Surface = BeginTerrainBoxEdits(terrain, ChunkCorner(id) - 2.0f * cell, ChunkCorner(id) + GetTerrainChunkSize(id.Level) + cell);
		glm::vec3 low;
		glm::vec3 high;
		GetTerrainChunkReach(id, low, high);
		field.Reach = BeginTerrainBoxEdits(terrain, low, high);
		return field;
	}

	const TerrainChunkField& TerrainChunkFields::Get(const SdfTerrainComponent& terrain, const TerrainChunkId& id, const TerrainEditIndex* index)
	{
		auto [it, bAdded] = Known.try_emplace(PackId(id));
		if (bAdded || it->second.Reach.Seen > terrain.Edits.size())
		{
			it->second = BeginTerrainChunkField(terrain, id);
		}
		ContinueTerrainBoxEdits(terrain, it->second.Surface, index);
		ContinueTerrainBoxEdits(terrain, it->second.Reach, index);
		return it->second;
	}

	void TerrainChunkFields::Rewind(const SdfTerrainComponent& terrain, size_t same)
	{
		for (auto& [packed, field] : Known)
		{
			RewindTerrainBoxEdits(terrain, field.Surface, same);
			RewindTerrainBoxEdits(terrain, field.Reach, same);
		}
	}

	double GetTerrainEditClock()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void NoteTerrainEdits(TerrainEditActivationComponent& activation, ETerrainEditPart part, uint64_t edits, double now)
	{
		(part == ETerrainEditPart::Draw ? activation.DrawSeen : activation.CollisionSeen) = now;
		if (activation.Latest == 0)
		{
			activation.Latest = edits;
			activation.Live = edits;
			return;
		}
		if (edits != activation.Latest)
		{
			activation.Latest = edits;
			if (activation.PendingSince < 0.0)
			{
				activation.PendingSince = now;
			}
		}
		if (activation.Latest == activation.Live)
		{
			activation.PendingSince = -1.0;
		}
	}

	bool ActivateTerrainEdits(TerrainEditActivationComponent& activation, ETerrainEditPart part, uint64_t edits, double now, float deadline)
	{
		if (edits == activation.Live)
		{
			return true;
		}
		const bool bDraw = part == ETerrainEditPart::Draw;
		(bDraw ? activation.DrawReady : activation.CollisionReady) = edits;
		const uint64_t otherReady = bDraw ? activation.CollisionReady : activation.DrawReady;
		const double otherSeen = bDraw ? activation.CollisionSeen : activation.DrawSeen;
		const bool bOther = otherReady == edits || otherSeen < 0.0 || now - otherSeen > TerrainEditPartTimeout;
		const bool bLate = activation.PendingSince >= 0.0 && now - activation.PendingSince > double(std::max(deadline, 0.0f));
		if (!bOther && !bLate)
		{
			return false;
		}
		activation.Live = edits;
		++activation.Activations;
		activation.Late += bOther ? 0u : 1u;
		activation.LastSeconds = activation.PendingSince >= 0.0 ? now - activation.PendingSince : 0.0;
		// Newer edits than these still to come live: from now
		activation.PendingSince = activation.Latest == activation.Live ? -1.0 : now;
		return true;
	}

	namespace
	{
		// A chunk as made: where, from which values and edits, against what round it
		uint64_t VersionOf(const TerrainChunkId& id, uint64_t shape, uint64_t edits, const TerrainNeighbourhood& neighbourhood)
		{
			Hasher hasher;
			hasher.Value(PackId(id));
			hasher.Value(shape);
			hasher.Value(edits);
			hasher.Value(neighbourhood.Hash());
			return hasher.Hash;
		}

		uint64_t VersionOf(const TerrainChunk& chunk)
		{
			return VersionOf({ chunk.Level, chunk.Key }, chunk.Shape, chunk.Edits, chunk.Neighbourhood);
		}

		// The chunks drawn, as made
		struct MadeChunk
		{
			uint64_t Edits = 0;
			std::shared_ptr<const TerrainChunkSamples> Samples;
		};
		std::unordered_map<uint64_t, MadeChunk> GetMadeChunks(const TerrainChunksComponent& chunks, uint64_t shape)
		{
			std::unordered_map<uint64_t, MadeChunk> made;
			for (const TerrainChunk& chunk : chunks.Chunks)
			{
				if (chunk.bDrawn && chunk.Shape == shape && chunk.Samples)
				{
					made.emplace(PackId({ chunk.Level, chunk.Key }), MadeChunk{ chunk.Edits, chunk.Samples });
				}
			}
			return made;
		}

		// Of a choice's leaves, whether the surface crosses each, and the edits each is wanted with:
		// those it is drawn with while no edit since can change it, else those reaching it
		void ChooseEdits(Detail::TerrainStreaming::Plan& plan, TerrainChunkFields& fields, TerrainChunkKeeps& keeps, const std::vector<uint64_t>& runs,
			const TerrainEditIndex& editIndex, size_t rewindTo, const std::unordered_map<uint64_t, MadeChunk>& made)
		{
			const SdfTerrainComponent& snapshot = *plan.Snapshot;
			if (rewindTo != std::numeric_limits<size_t>::max())
			{
				fields.Rewind(snapshot, rewindTo);
			}
			plan.Surface.resize(plan.Tree.Leaves.size());
			plan.ChunkEdits.resize(plan.Tree.Leaves.size());
			for (size_t index = 0; index < plan.Tree.Leaves.size(); ++index)
			{
				const TerrainChunkId& id = plan.Tree.Leaves[index];
				const TerrainChunkField& field = fields.Get(snapshot, id, &editIndex);
				plan.Surface[index] = field.HasSurface();
				plan.ChunkEdits[index] = field.GetEdits();
				if (const auto found = made.find(PackId(id)); found != made.end())
				{
					plan.ChunkEdits[index] = keeps.Get(snapshot, runs, id, found->second.Edits, found->second.Samples.get(), field.GetEdits());
				}
			}
		}

		// The chunks chosen on a worker, and their edits (ChooseEdits())
		void SchedulePlan(Detail::TerrainStreaming& streaming, const TerrainChunksComponent& chunks, const SdfTerrainComponent& terrain,
			std::vector<glm::vec3> viewpoints, JobSystem& jobs)
		{
			auto planning = std::make_shared<Detail::TerrainStreaming::Planning>();
			streaming.PlanInFlight = planning;
			std::shared_ptr<const SdfTerrainComponent> snapshot = streaming.Snapshot;
			std::shared_ptr<TerrainChunkFields> fields = streaming.Fields;
			std::shared_ptr<const std::vector<uint64_t>> runs = streaming.EditRuns;
			std::shared_ptr<TerrainChunkKeeps> keeps = streaming.Keeps;
			std::shared_ptr<const TerrainEditIndex> index = streaming.EditIndex;
			const size_t rewindTo = std::exchange(streaming.RewindTo, std::numeric_limits<size_t>::max());
			std::unordered_map<uint64_t, MadeChunk> made = GetMadeChunks(chunks, streaming.Shape);
			const TerrainLevelSettings settings = GetLevelSettings(terrain);
			TerrainChunkTree previous;
			previous.Split = streaming.Drawn.Split;
			const uint64_t shape = streaming.Shape;
			const uint64_t edits = streaming.Edits;
			streaming.PlanJob = jobs.Schedule(
				[planning, snapshot, fields, runs, keeps, index, rewindTo, made = std::move(made), settings, previous = std::move(previous), viewpoints = std::move(viewpoints), shape,
					edits]
				{
					const auto start = std::chrono::steady_clock::now();
					Detail::TerrainStreaming::Plan plan;
					plan.Shape = shape;
					plan.Edits = edits;
					plan.Snapshot = snapshot;
					plan.Viewpoints = viewpoints;
					plan.Tree = SelectTerrainChunks(viewpoints, settings, &previous);
					plan.Neighbourhoods = GetTerrainNeighbourhoods(plan.Tree);
					ChooseEdits(plan, *fields, *keeps, *runs, *index, rewindTo, made);
					plan.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
					std::lock_guard lock(planning->Lock);
					planning->Result = std::move(plan);
				},
				EJobPriority::Normal);
		}

		// A choice made: the next tree, unless its chunks are those drawn, and its edits live
		void TakePlan(TerrainChunksComponent& chunks, Detail::TerrainStreaming& streaming, Detail::TerrainStreaming::Plan& plan,
			TerrainEditActivationComponent& activation, float deadline, double now)
		{
			chunks.Stats.LastPlanMilliseconds = plan.Milliseconds;
			// Chosen for edits since changed: chosen again once these are drawn
			if (plan.Edits == streaming.Edits)
			{
				streaming.PlannedFrom = plan.Viewpoints;
			}
			streaming.ChosenFrom = plan.Viewpoints;
			std::vector<Detail::TerrainStreaming::Wanted> wanted;
			uint32_t skipped = 0;
			for (size_t index = 0; index < plan.Tree.Leaves.size(); ++index)
			{
				if (!plan.Surface[index])
				{
					++skipped;
					continue;
				}
				wanted.push_back({ plan.Tree.Leaves[index], plan.ChunkEdits[index], plan.Neighbourhoods[index], NearestViewpoint(plan.Viewpoints, plan.Tree.Leaves[index]) });
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
				bSame = drawn.contains(VersionOf(wanted[n].Id, streaming.Shape, wanted[n].Edits, wanted[n].Neighbourhood));
			}
			if (bSame && (plan.Edits == streaming.DrawnEdits || ActivateTerrainEdits(activation, ETerrainEditPart::Draw, plan.Edits, now, deadline)))
			{
				streaming.DrawnNeighbourhoods = std::move(plan.Neighbourhoods);
				streaming.Drawn = std::move(plan.Tree);
				streaming.DrawnEdits = plan.Edits;
				streaming.DrawnSkipped = skipped;
				return;
			}
			streaming.NextNeighbourhoods = std::move(plan.Neighbourhoods);
			streaming.Next = std::move(plan.Tree);
			streaming.NextChunks = std::move(wanted);
			streaming.NextSkipped = skipped;
			streaming.NextEdits = plan.Edits;
			streaming.NextSnapshot = std::move(plan.Snapshot);
			streaming.bNextNearDrawn = false;
		}

		void UpdateTerrain(Registry& world, Entity entity, const SdfTerrainComponent& terrain, std::span<const glm::vec3> worldViewpoints, JobSystem& jobs)
		{
			if (!world.Has<TerrainEditActivationComponent>(entity))
			{
				world.Add<TerrainEditActivationComponent>(entity, TerrainEditActivationComponent{});
			}
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
				streaming.Edits = HashTerrainEdits(terrain);
				streaming.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
				streaming.Fields = std::make_shared<TerrainChunkFields>();
				streaming.Keeps = std::make_shared<TerrainChunkKeeps>();
				std::vector<uint64_t> runs;
				HashTerrainEditRuns(terrain, runs);
				streaming.EditRuns = std::make_shared<const std::vector<uint64_t>>(std::move(runs));
				auto index = std::make_shared<TerrainEditIndex>();
				index->Update(terrain, 0);
				streaming.EditIndex = std::move(index);
				streaming.RewindTo = std::numeric_limits<size_t>::max();
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
			// New edits: a copy for the jobs, and the chunks chosen again as soon as the next ones are
			// drawn, what was known of them carried over, rewound to the first edit that changed (an
			// undo); the chunks being made go on, each kept if its edits are those it is wanted with
			else if (const size_t same = CountSameTerrainEdits(terrain, *streaming.Snapshot);
					 same != terrain.Edits.size() || same != streaming.Snapshot->Edits.size())
			{
				if (same < streaming.Snapshot->Edits.size())
				{
					streaming.RewindTo = std::min(streaming.RewindTo, same);
				}
				std::vector<uint64_t> runs(streaming.EditRuns->begin(), streaming.EditRuns->begin() + std::ptrdiff_t(same + 1));
				HashTerrainEditRuns(terrain, runs);
				streaming.EditRuns = std::make_shared<const std::vector<uint64_t>>(std::move(runs));
				auto index = std::make_shared<TerrainEditIndex>(*streaming.EditIndex);
				index->Update(terrain, same);
				streaming.EditIndex = std::move(index);
				streaming.Edits = HashTerrainEdits(terrain);
				streaming.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
				streaming.PlannedFrom.reset();
			}
			const double now = GetTerrainEditClock();
			TerrainEditActivationComponent& activation = world.Get<TerrainEditActivationComponent>(entity);
			NoteTerrainEdits(activation, ETerrainEditPart::Draw, streaming.Edits, now);

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
						TakePlan(chunks, streaming, *plan, activation, terrain.ActivationDeadline, now);
					}
				}
			}

			// Nothing being chosen or made: chosen again once a viewpoint has moved, or for new values or
			// edits; for them alone, the viewpoints still where the tree drawn was chosen, that tree's
			// chunks here at once, without a worker's round trip
			if (!streaming.PlanInFlight && !streaming.Next && !viewpoints.empty() &&
				(!streaming.PlannedFrom || HasMoved(*streaming.PlannedFrom, viewpoints, GetReplanDistance(terrain))))
			{
				// An edit taken away from far back makes the chunks go through every edit after it again:
				// on a worker
				const bool bDeepRewind = streaming.RewindTo != std::numeric_limits<size_t>::max() && streaming.Snapshot->Edits.size() > streaming.RewindTo + MaxEditsChosenHere;
				if (!bDeepRewind && streaming.ChosenFrom && !HasMoved(*streaming.ChosenFrom, viewpoints, GetReplanDistance(terrain)) && !streaming.Drawn.Leaves.empty() &&
					streaming.DrawnNeighbourhoods.size() == streaming.Drawn.Leaves.size())
				{
					const auto start = std::chrono::steady_clock::now();
					Detail::TerrainStreaming::Plan plan;
					plan.Shape = streaming.Shape;
					plan.Edits = streaming.Edits;
					plan.Snapshot = streaming.Snapshot;
					plan.Viewpoints = *streaming.ChosenFrom;
					plan.Tree = streaming.Drawn;
					plan.Neighbourhoods = streaming.DrawnNeighbourhoods;
					ChooseEdits(plan, *streaming.Fields, *streaming.Keeps, *streaming.EditRuns, *streaming.EditIndex,
					std::exchange(streaming.RewindTo, std::numeric_limits<size_t>::max()), GetMadeChunks(chunks, streaming.Shape));
					plan.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
					TakePlan(chunks, streaming, plan, activation, terrain.ActivationDeadline, now);
				}
				else
				{
					SchedulePlan(streaming, chunks, terrain, viewpoints, jobs);
				}
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
				wantedVersions.emplace(PackId(chunk.Id), VersionOf(chunk.Id, streaming.Shape, chunk.Edits, chunk.Neighbourhood));
			}
			for (Detail::TerrainStreaming::Done& done : finished)
			{
				const uint64_t packed = PackId(done.Id);
				const auto job = streaming.Jobs.find(packed);
				if (job != streaming.Jobs.end() && job->second.Shape == done.Shape && job->second.Edits == done.Edits &&
					job->second.Neighbourhood == done.Neighbourhood.Hash())
				{
					streaming.Jobs.erase(job);
				}
				const auto wanted = wantedVersions.find(packed);
				if (wanted == wantedVersions.end() || wanted->second != VersionOf(done.Id, done.Shape, done.Edits, done.Neighbourhood))
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
						made.Edits = chunk.Edits;
						made.Samples = std::move(ready->second.Samples);
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
						if (job->second.Shape == streaming.Shape && job->second.Edits == chunk.Edits && job->second.Neighbourhood == chunk.Neighbourhood.Hash())
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
					std::shared_ptr<const SdfTerrainComponent> snapshot = streaming.NextSnapshot;
					const TerrainChunkId id = chunk.Id;
					const TerrainNeighbourhood neighbourhood = chunk.Neighbourhood;
					const uint64_t jobShape = streaming.Shape;
					const uint64_t jobEdits = chunk.Edits;
					// For new edits on the tree drawn, waited for: soonest, its parts on every worker free
					const bool bEdit = streaming.NextEdits != streaming.DrawnEdits && streaming.Next->Leaves == streaming.Drawn.Leaves;
					FieldRunParts runParts;
					if (bEdit)
					{
						runParts = [&jobs](size_t count, const std::function<void(size_t)>& part) { jobs.ParallelFor(count, part); };
					}
					JobHandle handle = jobs.Schedule(
						[shared, snapshot, id, neighbourhood, jobShape, jobEdits, runParts]
						{
							Detail::TerrainStreaming::Done done;
							done.Id = id;
							done.Shape = jobShape;
							done.Edits = jobEdits;
							done.Neighbourhood = neighbourhood;
							auto samples = std::make_shared<TerrainChunkSamples>();
							done.Mesh = std::make_shared<const MeshData>(MakeTerrainChunkMesh(*snapshot, id, neighbourhood, &done.Times, samples.get(), runParts));
							done.Samples = std::move(samples);
							std::lock_guard lock(shared->Lock);
							shared->Finished.push_back(std::move(done));
						},
						bEdit ? EJobPriority::High : EJobPriority::Normal);
					streaming.Jobs.emplace(packed, Detail::TerrainStreaming::InFlight{ handle, jobShape, jobEdits, neighbourhood.Hash() });
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

				// New edits on the tree drawn: its near chunks, once all in and the edits live in collision
				// too, drawn at once in place of their old ones, the far levels left to follow
				const int nearLevel = ClampLevel(terrain.FinestLevel) + NearEditLevels;
				if (!bAllPresent && !streaming.bNextNearDrawn && streaming.NextEdits != streaming.DrawnEdits && streaming.Next->Leaves == streaming.Drawn.Leaves &&
					std::all_of(streaming.NextChunks.begin(), streaming.NextChunks.end(),
						[&](const Detail::TerrainStreaming::Wanted& chunk) { return chunk.Id.Level > nearLevel || present.contains(wantedVersions[PackId(chunk.Id)]); }) &&
					ActivateTerrainEdits(activation, ETerrainEditPart::Draw, streaming.NextEdits, now, terrain.ActivationDeadline))
				{
					std::unordered_set<uint64_t> near;
					for (const Detail::TerrainStreaming::Wanted& chunk : streaming.NextChunks)
					{
						if (chunk.Id.Level <= nearLevel)
						{
							near.insert(wantedVersions[PackId(chunk.Id)]);
						}
					}
					std::vector<TerrainChunk> replaced;
					std::erase_if(chunks.Chunks, [&](TerrainChunk& chunk)
						{
							const uint64_t packed = PackId({ chunk.Level, chunk.Key });
							if (!chunk.bDrawn || chunk.Level > nearLevel || near.contains(VersionOf(chunk)) || !wantedVersions.contains(packed) ||
								!near.contains(wantedVersions[packed]))
							{
								return false;
							}
							replaced.push_back(std::move(chunk));
							return true;
						});
					if (!replaced.empty())
					{
						jobs.Schedule([replaced = std::move(replaced)] {}, EJobPriority::Low);
					}
					for (TerrainChunk& chunk : chunks.Chunks)
					{
						chunk.bDrawn = chunk.bDrawn || near.contains(VersionOf(chunk));
					}
					streaming.bNextNearDrawn = true;
				}

				// All in, and their edits live in collision too: drawn at once in place of those drawn, so
				// seams always meet their neighbours
				if (bAllPresent &&
					(streaming.NextEdits == streaming.DrawnEdits ||
						ActivateTerrainEdits(activation, ETerrainEditPart::Draw, streaming.NextEdits, now, terrain.ActivationDeadline)))
				{
					std::unordered_set<uint64_t> next;
					for (const auto& [packed, version] : wantedVersions)
					{
						next.insert(version);
					}
					// Those replaced let go of on a worker: freeing hundreds of meshes at once costs the
					// main thread milliseconds
					std::vector<TerrainChunk> replaced;
					std::erase_if(chunks.Chunks, [&](TerrainChunk& chunk)
						{
							if (next.contains(VersionOf(chunk)))
							{
								return false;
							}
							replaced.push_back(std::move(chunk));
							return true;
						});
					if (!replaced.empty())
					{
						jobs.Schedule([replaced = std::move(replaced)] {}, EJobPriority::Low);
					}
					for (TerrainChunk& chunk : chunks.Chunks)
					{
						chunk.bDrawn = true;
					}
					streaming.Drawn = std::move(*streaming.Next);
					streaming.DrawnEdits = streaming.NextEdits;
					streaming.DrawnNeighbourhoods = std::move(streaming.NextNeighbourhoods);
					streaming.DrawnSkipped = streaming.NextSkipped;
					streaming.Next.reset();
					streaming.NextChunks.clear();
					streaming.NextSnapshot.reset();
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
			stats.bSettled = !streaming.Next && !streaming.PlanInFlight && streaming.PlannedFrom && streaming.DrawnEdits == streaming.Edits &&
							 !HasMoved(*streaming.PlannedFrom, viewpoints, GetReplanDistance(terrain));
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
		if (world.Has<TerrainEditActivationComponent>(entity))
		{
			world.Remove<TerrainEditActivationComponent>(entity);
		}
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

	std::vector<InspectedValue> DescribeTerrainEditActivation(const TerrainEditActivationComponent& component)
	{
		return {
			{ "Edits", component.Latest == component.Live ? std::string("live") : std::format("waiting for {}", component.DrawReady == component.Latest ? "collision" : "drawing") },
			{ "Made Live", std::format("{}, {} by the deadline", component.Activations, component.Late) },
			{ "Last", std::format("{:.0f} ms from the edits to live", component.LastSeconds * 1000.0) },
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
			{ "Edits", std::format("{} bodies waiting to go live; {} inside new terrain, {} lost support", stats.Waiting, stats.InsideNewTerrain, stats.LostSupport) },
		};
	}
}

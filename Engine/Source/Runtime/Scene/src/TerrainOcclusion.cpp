#include "NyxPCH.h"
#include "TerrainOcclusion.h"

#include "Entity.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TransformComponent.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>

namespace Nyx
{
	namespace Detail
	{
		// Cells of one level to fill, a box of them in cells of its size
		struct TerrainOcclusionBlock
		{
			int Level = 0;
			glm::ivec3 Min{ 0 };
			glm::ivec3 Count{ 0 };
			std::vector<uint8_t> Solid;
			uint64_t Sampled = 0;
		};

		struct TerrainOcclusionFill
		{
			// What it is filled from: the values and edits, their shape, the terrain's placement
			std::shared_ptr<const SdfTerrainComponent> From;
			uint64_t Shape = 0;
			glm::mat4 ToWorld{ 1.0f };
			// The windows it fills
			std::array<glm::ivec3, TerrainOcclusion::Levels> Origins{};
			std::vector<TerrainOcclusionBlock> Blocks;
			std::vector<JobHandle> Jobs;
			std::atomic<uint32_t> Remaining{ 0 };
			std::chrono::steady_clock::time_point Started;
			double Milliseconds = 0.0;
			bool bTaken = false;
		};
	}

	namespace
	{
		using namespace TerrainOcclusion;

		int FloorDiv(int value, int divisor)
		{
			return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
		}

		int Wrap(int value)
		{
			const int wrapped = value % Size;
			return wrapped < 0 ? wrapped + Size : wrapped;
		}

		size_t CellIndex(const glm::ivec3& cell)
		{
			return size_t(Wrap(cell.x)) + size_t(Wrap(cell.y)) * Size + size_t(Wrap(cell.z)) * Size * Size;
		}

		float CellSizeOf(int level)
		{
			return FinestCell * float(1 << level);
		}

		// The window round a point: the point's cell between the middle and a step short of it
		glm::ivec3 WindowRound(const glm::vec3& point, int level)
		{
			const glm::ivec3 cell = glm::ivec3(glm::floor(point / CellSizeOf(level)));
			return glm::ivec3(FloorDiv(cell.x, Step), FloorDiv(cell.y, Step), FloorDiv(cell.z, Step)) * Step - glm::ivec3(Size / 2);
		}

		// A box of cells, in pieces of at most a Block a side
		void AddBlocks(std::vector<Detail::TerrainOcclusionBlock>& blocks, int level, const glm::ivec3& min, const glm::ivec3& max)
		{
			for (int z = min.z; z < max.z; z += Block)
			{
				for (int y = min.y; y < max.y; y += Block)
				{
					for (int x = min.x; x < max.x; x += Block)
					{
						Detail::TerrainOcclusionBlock block;
						block.Level = level;
						block.Min = glm::ivec3(x, y, z);
						block.Count = glm::min(glm::ivec3(Block), max - block.Min);
						blocks.push_back(std::move(block));
					}
				}
			}
		}

		// The cells of the window at `to` that the window at `from` hasn't, in up to three slabs
		void AddNewCells(std::vector<Detail::TerrainOcclusionBlock>& blocks, int level, const glm::ivec3& from, const glm::ivec3& to)
		{
			const glm::ivec3 toMax = to + glm::ivec3(Size);
			const glm::ivec3 fromMax = from + glm::ivec3(Size);
			if (glm::any(glm::greaterThanEqual(glm::abs(to - from), glm::ivec3(Size))))
			{
				AddBlocks(blocks, level, to, toMax);
				return;
			}
			// What is kept along each axis, and the new cells either side of it
			glm::ivec3 keptMin = glm::max(to, from);
			glm::ivec3 keptMax = glm::min(toMax, fromMax);
			glm::ivec3 lower = to;
			glm::ivec3 upper = toMax;
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::ivec3 sliceMin = lower;
				glm::ivec3 sliceMax = upper;
				if (to[axis] < keptMin[axis])
				{
					sliceMax[axis] = keptMin[axis];
					AddBlocks(blocks, level, sliceMin, sliceMax);
				}
				sliceMin = lower;
				sliceMax = upper;
				if (toMax[axis] > keptMax[axis])
				{
					sliceMin[axis] = keptMax[axis];
					AddBlocks(blocks, level, sliceMin, sliceMax);
				}
				// The axes after this one only within what this one kept
				lower[axis] = keptMin[axis];
				upper[axis] = keptMax[axis];
			}
		}

		// A box in the world round a box in the terrain's space
		void ToWorldBox(const glm::mat4& toWorld, const glm::vec3& low, const glm::vec3& high, glm::vec3& outMin, glm::vec3& outMax)
		{
			outMin = glm::vec3(std::numeric_limits<float>::max());
			outMax = glm::vec3(-std::numeric_limits<float>::max());
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::vec3 point((corner & 1) ? high.x : low.x, (corner & 2) ? high.y : low.y, (corner & 4) ? high.z : low.z);
				const glm::vec3 world = glm::vec3(toWorld * glm::vec4(point, 1.0f));
				outMin = glm::min(outMin, world);
				outMax = glm::max(outMax, world);
			}
		}

		uint8_t ToSolid(float value, float cellSize)
		{
			return uint8_t(std::lround(std::clamp(0.5f - value / cellSize, 0.0f, 1.0f) * 255.0f));
		}

		// A block's cells sampled at their middles, with only the edits reaching it; all air or all
		// solid by the value range without sampling
		void FillBlock(const Detail::TerrainOcclusionFill& fill, Detail::TerrainOcclusionBlock& block)
		{
			const float cell = CellSizeOf(block.Level);
			const size_t count = size_t(block.Count.x) * block.Count.y * block.Count.z;
			const glm::mat4 toTerrain = glm::inverse(fill.ToWorld);
			glm::vec3 low;
			glm::vec3 high;
			ToWorldBox(toTerrain, glm::vec3(block.Min) * cell, glm::vec3(block.Min + block.Count) * cell, low, high);
			SdfTerrainComponent terrain = *fill.From;
			KeepTerrainEditsReaching(terrain, low, high);
			const TerrainValueRange range = GetTerrainValueRange(terrain, low, high);
			if (range.Lowest >= 0.5f * cell || range.Highest <= -0.5f * cell)
			{
				block.Solid.assign(count, range.Lowest >= 0.5f * cell ? uint8_t(0) : uint8_t(255));
				return;
			}
			std::vector<float> x(count);
			std::vector<float> y(count);
			std::vector<float> z(count);
			size_t n = 0;
			for (int k = 0; k < block.Count.z; ++k)
			{
				for (int j = 0; j < block.Count.y; ++j)
				{
					for (int i = 0; i < block.Count.x; ++i, ++n)
					{
						const glm::vec3 middle = (glm::vec3(block.Min + glm::ivec3(i, j, k)) + 0.5f) * cell;
						const glm::vec3 local = glm::vec3(toTerrain * glm::vec4(middle, 1.0f));
						x[n] = local.x;
						y[n] = local.y;
						z[n] = local.z;
					}
				}
			}
			std::vector<float> values(count);
			SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), count, values.data());
			block.Solid.resize(count);
			for (size_t m = 0; m < count; ++m)
			{
				block.Solid[m] = ToSolid(values[m], cell);
			}
			block.Sampled = count;
		}

		void TakeFill(TerrainOcclusionComponent& occlusion, Detail::TerrainOcclusionFill& fill)
		{
			fill.bTaken = true;
			std::array<bool, Levels> changed{};
			for (int level = 0; level < Levels; ++level)
			{
				TerrainOcclusionLevel& into = occlusion.Levels[size_t(level)];
				changed[size_t(level)] = into.Origin != fill.Origins[size_t(level)] || !occlusion.bReady;
				into.Origin = fill.Origins[size_t(level)];
			}
			uint64_t sampled = 0;
			for (const Detail::TerrainOcclusionBlock& block : fill.Blocks)
			{
				TerrainOcclusionLevel& into = occlusion.Levels[size_t(block.Level)];
				size_t n = 0;
				for (int k = 0; k < block.Count.z; ++k)
				{
					for (int j = 0; j < block.Count.y; ++j)
					{
						for (int i = 0; i < block.Count.x; ++i, ++n)
						{
							into.Solid[CellIndex(block.Min + glm::ivec3(i, j, k))] = block.Solid[n];
						}
					}
				}
				changed[size_t(block.Level)] = true;
				sampled += block.Sampled;
			}
			for (int level = 0; level < Levels; ++level)
			{
				occlusion.Levels[size_t(level)].Revision += changed[size_t(level)] ? 1 : 0;
			}
			occlusion.bReady = true;
			occlusion.bFilling = false;
			++occlusion.Fills;
			occlusion.LastSampled = sampled;
			occlusion.LastMilliseconds = fill.Milliseconds;
			fill.Blocks.clear();
			fill.Blocks.shrink_to_fit();
		}

		void UpdateOne(Registry& world, Entity entity, const glm::vec3& viewpoint, JobSystem& jobs)
		{
			const SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(entity);
			TerrainOcclusionComponent& occlusion = world.Get<TerrainOcclusionComponent>(entity);
			occlusion.Strength = std::clamp(terrain.SkyOcclusion, 0.0f, 1.0f);
			std::shared_ptr<Detail::TerrainOcclusionFill>& last = occlusion.Fill;

			// Done: what it made goes in, all at once
			if (last && !last->bTaken)
			{
				if (last->Remaining.load(std::memory_order_acquire) != 0)
				{
					return;
				}
				TakeFill(occlusion, *last);
			}

			const glm::mat4 toWorld = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).ToMatrix() : glm::mat4(1.0f);
			const uint64_t shape = HashSdfTerrainShape(terrain);
			std::array<glm::ivec3, Levels> origins;
			for (int level = 0; level < Levels; ++level)
			{
				origins[size_t(level)] = WindowRound(viewpoint, level);
			}
			const bool bWhole = !occlusion.bReady || !last || last->Shape != shape || last->ToWorld != toWorld;
			const size_t same = bWhole ? 0 : CountSameTerrainEdits(terrain, *last->From);
			const bool bEdited = !bWhole && (same != terrain.Edits.size() || same != last->From->Edits.size());
			bool bMoved = false;
			for (int level = 0; level < Levels; ++level)
			{
				bMoved = bMoved || origins[size_t(level)] != occlusion.Levels[size_t(level)].Origin;
			}
			if (!bWhole && !bEdited && !bMoved)
			{
				return;
			}

			auto next = std::make_shared<Detail::TerrainOcclusionFill>();
			next->From = bEdited || bWhole ? std::make_shared<const SdfTerrainComponent>(terrain) : last->From;
			next->Shape = shape;
			next->ToWorld = toWorld;
			next->Origins = origins;
			for (int level = 0; level < Levels; ++level)
			{
				if (occlusion.Levels[size_t(level)].Solid.empty())
				{
					occlusion.Levels[size_t(level)].Solid.assign(size_t(Size) * Size * Size, 0);
					occlusion.Levels[size_t(level)].CellSize = CellSizeOf(level);
				}
				const glm::ivec3 to = origins[size_t(level)];
				if (bWhole)
				{
					AddBlocks(next->Blocks, level, to, to + glm::ivec3(Size));
				}
				else
				{
					AddNewCells(next->Blocks, level, occlusion.Levels[size_t(level)].Origin, to);
				}
			}
			// The edits that differ, theirs and those they replaced: their cells again where the
			// windows keep them (bakes change nothing)
			if (bEdited)
			{
				glm::vec3 changedMin(std::numeric_limits<float>::max());
				glm::vec3 changedMax(-std::numeric_limits<float>::max());
				auto widen = [&](const std::vector<SdfTerrainEdit>& edits)
				{
					for (size_t index = same; index < edits.size(); ++index)
					{
						if (edits[index].Kind == ESdfTerrainEditKind::Bake)
						{
							continue;
						}
						glm::vec3 low;
						glm::vec3 high;
						GetTerrainEditBounds(edits[index], low, high);
						glm::vec3 worldLow;
						glm::vec3 worldHigh;
						ToWorldBox(toWorld, low, high, worldLow, worldHigh);
						changedMin = glm::min(changedMin, worldLow);
						changedMax = glm::max(changedMax, worldHigh);
					}
				};
				widen(terrain.Edits);
				widen(last->From->Edits);
				if (changedMin.x <= changedMax.x)
				{
					for (int level = 0; level < Levels; ++level)
					{
						const float cell = CellSizeOf(level);
						const glm::ivec3 window = origins[size_t(level)];
						const glm::ivec3 min = glm::max(glm::ivec3(glm::floor(changedMin / cell)), window);
						const glm::ivec3 max = glm::min(glm::ivec3(glm::floor(changedMax / cell)) + 1, window + glm::ivec3(Size));
						if (glm::all(glm::lessThan(min, max)))
						{
							AddBlocks(next->Blocks, level, min, max);
						}
					}
				}
			}

			last = next;
			if (next->Blocks.empty())
			{
				// Only the windows moved within what they had, or only bakes came or went
				TakeFill(occlusion, *next);
				return;
			}
			occlusion.bFilling = true;
			next->Started = std::chrono::steady_clock::now();
			next->Remaining.store(static_cast<uint32_t>(next->Blocks.size()), std::memory_order_release);
			Detail::TerrainOcclusionFill* fill = next.get();
			for (size_t index = 0; index < next->Blocks.size(); ++index)
			{
				next->Jobs.push_back(jobs.Schedule(
					[fill, index, keep = next]
					{
						FillBlock(*fill, fill->Blocks[index]);
						if (fill->Remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
						{
							fill->Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fill->Started).count();
						}
					},
					EJobPriority::Normal));
			}
		}
	}

	void UpdateTerrainOcclusion(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs)
	{
		std::vector<Entity> gone;
		world.Each<TerrainOcclusionComponent>([&](Entity entity, TerrainOcclusionComponent&)
			{
				if (!world.Has<SdfTerrainComponent>(entity) || world.Get<SdfTerrainComponent>(entity).SkyOcclusion <= 0.0f)
				{
					gone.push_back(entity);
				}
			});
		for (const Entity entity : gone)
		{
			if (const std::shared_ptr<Detail::TerrainOcclusionFill>& fill = world.Get<TerrainOcclusionComponent>(entity).Fill)
			{
				for (const JobHandle& job : fill->Jobs)
				{
					jobs.Cancel(job);
				}
			}
			world.Remove<TerrainOcclusionComponent>(entity);
		}
		if (viewpoints.empty())
		{
			return;
		}
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent& terrain)
			{
				if (terrain.SkyOcclusion > 0.0f && !terrain.Layers.empty())
				{
					terrains.push_back(entity);
				}
			});
		for (const Entity entity : terrains)
		{
			if (!world.Has<TerrainOcclusionComponent>(entity))
			{
				world.Add<TerrainOcclusionComponent>(entity, TerrainOcclusionComponent{});
			}
			UpdateOne(world, entity, viewpoints.front(), jobs);
		}
	}

	float SampleTerrainOcclusion(const TerrainOcclusionComponent& occlusion, int level, const glm::vec3& point)
	{
		const TerrainOcclusionLevel& in = occlusion.Levels[size_t(level)];
		if (in.Solid.empty() || glm::any(glm::lessThan(point, in.GetWindowMin())) || glm::any(glm::greaterThanEqual(point, in.GetWindowMax())))
		{
			return -1.0f;
		}
		// Between the middles of the eight cells round it, as a GPU's linear filter
		const glm::vec3 at = point / in.CellSize - 0.5f;
		const glm::ivec3 first = glm::ivec3(glm::floor(at));
		const glm::vec3 f = at - glm::vec3(first);
		float value = 0.0f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const glm::ivec3 offset((corner & 1), (corner >> 1) & 1, (corner >> 2) & 1);
			const glm::vec3 weight = glm::mix(1.0f - f, f, glm::vec3(offset));
			value += weight.x * weight.y * weight.z * float(in.Solid[CellIndex(first + offset)]) / 255.0f;
		}
		return value;
	}

	float GetTerrainSkyVisibility(const TerrainOcclusionComponent& occlusion, const glm::vec3& point, const glm::vec3& normal)
	{
		if (!occlusion.bReady)
		{
			return 1.0f;
		}
		// The cones: along the normal, and round it tilted towards its sides, weighted by how much
		// they face the way it does
		const glm::vec3 n = glm::normalize(normal);
		const glm::vec3 other = std::fabs(n.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 u = glm::normalize(glm::cross(n, other));
		const glm::vec3 v = glm::cross(n, u);
		const glm::vec3 start = point + n * FinestCell;
		float seen = 0.0f;
		float weights = 0.0f;
		for (int cone = 0; cone < Cones; ++cone)
		{
			glm::vec3 direction = n;
			float weight = 1.0f;
			if (cone > 0)
			{
				const float around = 6.2831853f * float(cone - 1) / float(Cones - 1);
				direction = n * std::cos(ConeTilt) + (u * std::cos(around) + v * std::sin(around)) * std::sin(ConeTilt);
				weight = std::cos(ConeTilt);
			}
			float through = 1.0f;
			float t = FinestCell;
			for (int step = 0; step < MaxSteps && t < MaxDistance && through > 0.01f; ++step)
			{
				const float width = std::max(2.0f * t * ConeTangent, FinestCell);
				const glm::vec3 at = start + direction * t;
				int level = std::clamp(int(std::floor(std::log2(width / FinestCell))), 0, Levels - 1);
				float solid = SampleTerrainOcclusion(occlusion, level, at);
				while (solid < 0.0f && level + 1 < Levels)
				{
					solid = SampleTerrainOcclusion(occlusion, ++level, at);
				}
				if (solid < 0.0f)
				{
					break;
				}
				const float stride = StepWidths * width;
				through *= std::pow(1.0f - std::min(solid, 0.999f), stride / occlusion.Levels[size_t(level)].CellSize);
				t += stride;
			}
			seen += through * weight;
			weights += weight;
		}
		return glm::mix(1.0f, seen / weights, occlusion.Strength);
	}

	std::vector<InspectedValue> DescribeTerrainOcclusion(const TerrainOcclusionComponent& component)
	{
		if (!component.bReady)
		{
			return { { "Volume", "being filled" } };
		}
		const TerrainOcclusionLevel& finest = component.Levels.front();
		const TerrainOcclusionLevel& coarsest = component.Levels.back();
		return { { "Levels", std::format("{} of {} cells a side, {} m to {} m cells", Levels, Size, finest.CellSize, coarsest.CellSize) },
			{ "Fills", std::format("{}, the last {} cells sampled in {:.1f} ms", component.Fills, component.LastSampled, component.LastMilliseconds) } };
	}
}

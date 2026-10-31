#include "NyxPCH.h"
#include "TerrainWater.h"

#include "Entity.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainWorldPlan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <mutex>
#include <queue>
#include <utility>

namespace Nyx
{
	namespace
	{
		constexpr float Cell = TerrainWater::CellSize;
		// How much of a river's depth its water fills
		constexpr float RiverFill = 0.8f;
		// How far below its planned bed the ground must be for a river to fall
		constexpr float CutBelowBed = 3.0f;
		// How far a fall into the air is drawn
		constexpr float FallIntoAir = 400.0f;
		// The most cells a lake or a pool fills, and the most steps a river runs on from a fall
		constexpr size_t MostLakeCells = 40000;
		constexpr int MostRunSteps = 600;

		using EBody = TerrainWater::EBody;

		// The ground's search down a column: samples this far apart, 64 at a time, then the crossing
		// halved to a couple of centimetres. It can step over ground thinner than a step.
		constexpr float ColumnStep = 1.5f;
		constexpr int ColumnBatch = 64;
		constexpr int Halvings = 7;

		// The first surface down a column from `top` to `bottom`; NoGround if there is none
		float FindGround(const SdfTerrainComponent& terrain, const glm::vec2& point, float top, float bottom)
		{
			std::array<float, ColumnBatch> x;
			std::array<float, ColumnBatch> y;
			std::array<float, ColumnBatch> z;
			std::array<float, ColumnBatch> values;
			x.fill(point.x);
			z.fill(point.y);
			float above = top;
			if (SampleTerrainValue(terrain, glm::vec3(point.x, top, point.y)) <= 0.0f)
			{
				return top;
			}
			for (float from = top; from > bottom; from -= ColumnStep * float(ColumnBatch))
			{
				for (int n = 0; n < ColumnBatch; ++n)
				{
					y[size_t(n)] = from - ColumnStep * float(n + 1);
				}
				SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), size_t(ColumnBatch), values.data());
				for (int n = 0; n < ColumnBatch; ++n)
				{
					if (y[size_t(n)] < bottom)
					{
						return TerrainWater::NoGround;
					}
					if (values[size_t(n)] <= 0.0f)
					{
						// Between the last sample above and this one, halved
						float below = y[size_t(n)];
						for (int halving = 0; halving < Halvings; ++halving)
						{
							const float middle = (above + below) * 0.5f;
							if (SampleTerrainValue(terrain, glm::vec3(point.x, middle, point.y)) > 0.0f)
							{
								above = middle;
							}
							else
							{
								below = middle;
							}
						}
						return (above + below) * 0.5f;
					}
					above = y[size_t(n)];
				}
			}
			return TerrainWater::NoGround;
		}

		bool HasGround(float ground)
		{
			return ground > TerrainWater::NoGround * 0.5f;
		}

		// The ground under a point, blended between the cells round it; none where any of them has none
		float GroundAt(const TerrainWater& water, const glm::vec2& point)
		{
			const glm::vec2 at = (point - water.Origin) / Cell;
			const int i = std::clamp(int(std::floor(at.x)), 0, water.Cells - 2);
			const int j = std::clamp(int(std::floor(at.y)), 0, water.Cells - 2);
			const float tx = std::clamp(at.x - float(i), 0.0f, 1.0f);
			const float tz = std::clamp(at.y - float(j), 0.0f, 1.0f);
			const float a = water.Ground[water.IndexOf(i, j)];
			const float b = water.Ground[water.IndexOf(i + 1, j)];
			const float c = water.Ground[water.IndexOf(i, j + 1)];
			const float d = water.Ground[water.IndexOf(i + 1, j + 1)];
			if (!HasGround(a) || !HasGround(b) || !HasGround(c) || !HasGround(d))
			{
				return TerrainWater::NoGround;
			}
			return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
		}

		struct Neighbour
		{
			int Di;
			int Dj;
		};
		constexpr Neighbour Sides[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		constexpr Neighbour Around[8] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };

		// Fills a basin from a cell up to where it spills: cells taken lowest first, the level rising to
		// each, until the next is lower than the level (the water runs out there), has no ground (it
		// pours off) or holds other water. Returns the level and the cell it spills at; the cells under
		// the level in `outCells`.
		struct Fill
		{
			float Level = 0.0f;
			int Spill = -1;
			bool bOverEdge = false;
		};

		Fill FillBasin(const TerrainWater& water, int start, std::vector<int>& outCells)
		{
			using Entry = std::pair<float, int>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
			std::vector<uint8_t> seen(water.Ground.size(), 0);
			std::vector<int> taken;
			Fill fill;
			fill.Level = water.Ground[size_t(start)];
			open.emplace(fill.Level, start);
			seen[size_t(start)] = 1;
			while (!open.empty() && taken.size() < MostLakeCells)
			{
				const auto [ground, at] = open.top();
				open.pop();
				const bool bOther = water.Body[size_t(at)] != EBody::None;
				if (!HasGround(ground) || ground < fill.Level || bOther)
				{
					fill.Spill = at;
					fill.bOverEdge = !HasGround(ground);
					break;
				}
				fill.Level = ground;
				taken.push_back(at);
				const int i = at % water.Cells;
				const int j = at / water.Cells;
				for (const Neighbour& side : Sides)
				{
					const int ni = i + side.Di;
					const int nj = j + side.Dj;
					if (ni < 0 || nj < 0 || ni >= water.Cells || nj >= water.Cells)
					{
						continue;
					}
					const int next = int(water.IndexOf(ni, nj));
					if (seen[size_t(next)] == 0)
					{
						seen[size_t(next)] = 1;
						open.emplace(water.Ground[size_t(next)], next);
					}
				}
			}
			outCells.clear();
			for (const int at : taken)
			{
				if (water.Ground[size_t(at)] < fill.Level)
				{
					outCells.push_back(at);
				}
			}
			return fill;
		}

		void AddFall(TerrainWater& water, const glm::vec3& top, const glm::vec2& along, float halfWidth, float bottom)
		{
			const glm::vec2 across = glm::length(along) > 0.0f ? glm::normalize(glm::vec2(-along.y, along.x)) : glm::vec2(1.0f, 0.0f);
			water.Falls.push_back({ top, glm::vec3(across.x, 0.0f, across.y) * halfWidth, bottom });
		}

		// A river run on from a cell downhill over the ground: to the lowest cell round it while it is
		// lower; pooled where none is, on from where the pool spills; ended at other water or a fall off
		// an edge
		void RunOn(TerrainWater& water, TerrainWater::River& river, int at, float halfWidth)
		{
			std::vector<int> pool;
			for (int step = 0; step < MostRunSteps; ++step)
			{
				const int i = at % water.Cells;
				const int j = at / water.Cells;
				const glm::vec2 point = water.PointOf(i, j);
				const float ground = water.Ground[size_t(at)];
				river.Points.push_back(glm::vec3(point.x, ground + 0.3f, point.y));
				int lowest = -1;
				float lowestGround = ground;
				for (const Neighbour& side : Around)
				{
					const int ni = i + side.Di;
					const int nj = j + side.Dj;
					if (ni < 0 || nj < 0 || ni >= water.Cells || nj >= water.Cells)
					{
						continue;
					}
					const int next = int(water.IndexOf(ni, nj));
					if (water.Body[size_t(next)] != EBody::None)
					{
						return;
					}
					if (!HasGround(water.Ground[size_t(next)]))
					{
						const glm::vec2 out = water.PointOf(ni, nj);
						AddFall(water, glm::vec3((point.x + out.x) * 0.5f, ground + 0.3f, (point.y + out.y) * 0.5f), out - point, halfWidth, ground - FallIntoAir);
						return;
					}
					if (water.Ground[size_t(next)] < lowestGround)
					{
						lowestGround = water.Ground[size_t(next)];
						lowest = next;
					}
				}
				if (lowest >= 0)
				{
					at = lowest;
					continue;
				}
				// A hollow: a pool to where it spills, and on from there
				const Fill fill = FillBasin(water, at, pool);
				for (const int cell : pool)
				{
					water.Body[size_t(cell)] = EBody::Lake;
					water.Level[size_t(cell)] = fill.Level;
				}
				if (fill.Spill < 0 || fill.bOverEdge || water.Body[size_t(fill.Spill)] != EBody::None || pool.empty())
				{
					return;
				}
				at = fill.Spill;
			}
		}
	}

	glm::ivec2 TerrainWater::CellOf(const glm::vec2& point) const
	{
		const glm::vec2 at = (point - Origin) / CellSize;
		return glm::ivec2(std::clamp(int(std::floor(at.x + 0.5f)), 0, Cells - 1), std::clamp(int(std::floor(at.y + 0.5f)), 0, Cells - 1));
	}

	TerrainWater MakeTerrainWater(const SdfTerrainComponent& terrain, const TerrainWater* earlier, const glm::vec3* changedMin, const glm::vec3* changedMax,
		JobSystem* jobs)
	{
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		const SdfTerrainWorldPlan& made = plan->Made;
		TerrainWater water;
		water.Cells = int(std::ceil(float(plan->Nodes - 1) * TerrainWorldPlan::CellSize / Cell)) + 1;
		water.Origin = plan->Origin;
		const size_t count = size_t(water.Cells) * size_t(water.Cells);
		const bool bKeep = earlier != nullptr && earlier->Cells == water.Cells && earlier->Origin == water.Origin;
		water.Ground = bKeep ? earlier->Ground : std::vector<float>(count, TerrainWater::NoGround);
		water.Body.assign(count, EBody::None);
		water.Level.assign(count, 0.0f);

		// The ground: down each cell's middle from above the highest the plan's top can be there to below
		// its lowest underside; only in the changed box when the rest is kept
		const float lowest = -made.Thickness - std::fabs(made.UndersideDepth) - std::fabs(made.RiftDepth) - 50.0f;
		float heaped = -std::numeric_limits<float>::max();
		for (const SdfTerrainEdit& edit : terrain.Edits)
		{
			if (edit.Kind == ESdfTerrainEditKind::Shape && edit.Mode == ESdfTerrainEditMode::Add)
			{
				glm::vec3 low;
				glm::vec3 high;
				GetTerrainEditBounds(edit, low, high);
				heaped = std::max(heaped, high.y);
			}
		}
		auto findRow = [&](size_t row)
		{
			const int j = int(row);
			for (int i = 0; i < water.Cells; ++i)
			{
				const glm::vec2 point = water.PointOf(i, j);
				if (bKeep && changedMin != nullptr && changedMax != nullptr
					&& (point.x < changedMin->x - Cell || point.x > changedMax->x + Cell || point.y < changedMin->z - Cell || point.y > changedMax->z + Cell))
				{
					continue;
				}
				const TerrainWorldPlan::Bounds bounds = plan->GetRange(point - Cell, point + Cell);
				const float top = std::max(bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f, heaped) + 4.0f;
				const float bottom = bounds.LowHeight + lowest;
				water.Ground[water.IndexOf(i, j)] = FindGround(terrain, point, top, bottom);
			}
		};
		if (jobs != nullptr)
		{
			jobs->ParallelFor(size_t(water.Cells), findRow, EJobPriority::Normal);
		}
		else
		{
			for (size_t row = 0; row < size_t(water.Cells); ++row)
			{
				findRow(row);
			}
		}

		// The sea, from the plan's sea where the ground is below its level, out across all ground below
		// it; a fall wherever it reaches a place with none
		std::vector<int> open;
		for (int j = 0; j < water.Cells; ++j)
		{
			for (int i = 0; i < water.Cells; ++i)
			{
				const size_t index = water.IndexOf(i, j);
				const glm::ivec2 node = plan->NodeOf(water.PointOf(i, j));
				if (plan->Landform[plan->IndexOf(node.x, node.y)] == uint8_t(ESdfTerrainLandform::Sea) && HasGround(water.Ground[index])
					&& water.Ground[index] < made.SeaLevel)
				{
					water.Body[index] = EBody::Sea;
					open.push_back(int(index));
				}
			}
		}
		// It pours off the edge down the landmass's side, and into what is far deeper than its floor
		const float seaFallBottom = made.SeaLevel - made.Thickness;
		const float deep = made.SeaLevel - made.SeaDepth - 20.0f;
		while (!open.empty())
		{
			const int at = open.back();
			open.pop_back();
			water.Level[size_t(at)] = made.SeaLevel;
			const int i = at % water.Cells;
			const int j = at / water.Cells;
			for (const Neighbour& side : Sides)
			{
				const int ni = i + side.Di;
				const int nj = j + side.Dj;
				if (ni < 0 || nj < 0 || ni >= water.Cells || nj >= water.Cells)
				{
					continue;
				}
				const size_t next = water.IndexOf(ni, nj);
				if (!HasGround(water.Ground[next]) || (water.Ground[next] < deep && water.Body[next] == EBody::None))
				{
					const glm::vec2 a = water.PointOf(i, j);
					const glm::vec2 b = water.PointOf(ni, nj);
					const float bottom = HasGround(water.Ground[next]) ? water.Ground[next] : seaFallBottom;
					AddFall(water, glm::vec3((a.x + b.x) * 0.5f, made.SeaLevel, (a.y + b.y) * 0.5f), b - a, Cell * 0.5f, bottom);
				}
				else if (water.Body[next] == EBody::None && water.Ground[next] < made.SeaLevel)
				{
					water.Body[next] = EBody::Sea;
					open.push_back(int(next));
				}
			}
		}

		// Lakes, each from the lowest ground downhill of its middle to where it spills
		std::vector<int> cells;
		for (const TerrainWorldPlan::Lake& lake : plan->Lakes)
		{
			const glm::ivec2 cell = water.CellOf(lake.Center);
			int start = int(water.IndexOf(cell.x, cell.y));
			for (int step = 0; step < 64 && HasGround(water.Ground[size_t(start)]); ++step)
			{
				int lowest = start;
				for (const Neighbour& side : Around)
				{
					const int ni = start % water.Cells + side.Di;
					const int nj = start / water.Cells + side.Dj;
					if (ni >= 0 && nj >= 0 && ni < water.Cells && nj < water.Cells && HasGround(water.Ground[water.IndexOf(ni, nj)])
						&& water.Ground[water.IndexOf(ni, nj)] < water.Ground[size_t(lowest)])
					{
						lowest = int(water.IndexOf(ni, nj));
					}
				}
				if (lowest == start)
				{
					break;
				}
				start = lowest;
			}
			if (!HasGround(water.Ground[size_t(start)]) || water.Body[size_t(start)] != EBody::None)
			{
				continue;
			}
			const Fill fill = FillBasin(water, start, cells);
			for (const int at : cells)
			{
				water.Body[size_t(at)] = EBody::Lake;
				water.Level[size_t(at)] = fill.Level;
			}
		}

		// Rivers along their beds, falling where the ground under them is gone
		const float halfWidth = std::max(made.RiverWidth, 0.0f) * 0.5f;
		for (const TerrainWorldPlan::River& planned : plan->Rivers)
		{
			TerrainWater::River river;
			river.Width = std::max(made.RiverWidth, 0.0f);
			for (size_t n = 0; n < planned.Points.size(); ++n)
			{
				const glm::vec2 point = planned.Points[n];
				const float level = planned.Beds[n] + made.RiverDepth * RiverFill;
				const glm::ivec2 cell = water.CellOf(point);
				const size_t index = water.IndexOf(cell.x, cell.y);
				if (water.Body[index] == EBody::Sea && level <= water.Level[index] + 0.5f)
				{
					river.Points.push_back(glm::vec3(point.x, water.Level[index], point.y));
					break;
				}
				if (water.Body[index] == EBody::Lake)
				{
					// Through a lake: a stretch into it, another out of it
					if (!river.Points.empty())
					{
						river.Points.push_back(glm::vec3(point.x, water.Level[index], point.y));
						water.Rivers.push_back(std::move(river));
						river = TerrainWater::River{};
						river.Width = std::max(made.RiverWidth, 0.0f);
					}
					continue;
				}
				const float ground = GroundAt(water, point);
				if (HasGround(ground) && ground >= planned.Beds[n] - CutBelowBed)
				{
					river.Points.push_back(glm::vec3(point.x, level, point.y));
					continue;
				}
				// The ground under its bed is gone: it falls, to the ground below or into the air, and runs
				// on from where it lands
				const glm::vec2 along = n + 1 < planned.Points.size() ? planned.Points[n + 1] - point : point - planned.Points[n - (n > 0 ? 1 : 0)];
				const float below = HasGround(water.Ground[index]) ? water.Ground[index] : level - FallIntoAir;
				river.Points.push_back(glm::vec3(point.x, level, point.y));
				AddFall(water, glm::vec3(point.x, level, point.y), along, halfWidth, below);
				water.Rivers.push_back(std::move(river));
				river = TerrainWater::River{};
				river.Width = std::max(made.RiverWidth, 0.0f);
				if (HasGround(water.Ground[index]) && water.Body[index] == EBody::None)
				{
					RunOn(water, river, int(index), halfWidth);
				}
				break;
			}
			if (river.Points.size() > 1)
			{
				water.Rivers.push_back(std::move(river));
			}
		}
		return water;
	}

	MeshData MakeTerrainWaterMesh(const TerrainWater& water)
	{
		MeshData mesh;
		const glm::vec3 seaColour(0.05f, 0.16f, 0.32f);
		const glm::vec3 lakeColour(0.08f, 0.24f, 0.38f);
		const glm::vec3 fallColour(0.62f, 0.72f, 0.78f);
		auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& normal, const glm::vec3& colour)
		{
			const uint32_t first = uint32_t(mesh.Vertices.size());
			for (const glm::vec3& position : { a, b, c, d })
			{
				Vertex vertex;
				vertex.Position = position;
				vertex.Color = colour;
				vertex.Normal = normal;
				mesh.Vertices.push_back(vertex);
			}
			for (const uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u })
			{
				mesh.Indices.push_back(first + index);
			}
		};

		// The sea and lakes over their cells and the cells beside them, at the highest water beside a
		// cell
		const float half = TerrainWater::CellSize * 0.5f;
		for (int j = 0; j < water.Cells; ++j)
		{
			for (int i = 0; i < water.Cells; ++i)
			{
				const size_t index = water.IndexOf(i, j);
				if (!HasGround(water.Ground[index]))
				{
					continue;
				}
				EBody body = water.Body[index];
				float level = body != EBody::None ? water.Level[index] : -std::numeric_limits<float>::max();
				for (const Neighbour& side : Around)
				{
					const int ni = i + side.Di;
					const int nj = j + side.Dj;
					if (body == EBody::None && ni >= 0 && nj >= 0 && ni < water.Cells && nj < water.Cells && water.Body[water.IndexOf(ni, nj)] != EBody::None
						&& water.Level[water.IndexOf(ni, nj)] > level)
					{
						level = water.Level[water.IndexOf(ni, nj)];
						body = water.Body[water.IndexOf(ni, nj)];
					}
				}
				if (body == EBody::None)
				{
					continue;
				}
				const glm::vec2 point = water.PointOf(i, j);
				quad(glm::vec3(point.x - half, level, point.y - half), glm::vec3(point.x - half, level, point.y + half), glm::vec3(point.x + half, level, point.y + half),
					glm::vec3(point.x + half, level, point.y - half), glm::vec3(0.0f, 1.0f, 0.0f), body == EBody::Sea ? seaColour : lakeColour);
			}
		}

		// Rivers: a ribbon along each, its sides across its way
		for (const TerrainWater::River& river : water.Rivers)
		{
			for (size_t n = 0; n + 1 < river.Points.size(); ++n)
			{
				const glm::vec3 a = river.Points[n];
				const glm::vec3 b = river.Points[n + 1];
				const glm::vec2 way(b.x - a.x, b.z - a.z);
				if (glm::length(way) < 1e-3f)
				{
					continue;
				}
				const glm::vec2 side = glm::normalize(glm::vec2(-way.y, way.x)) * (river.Width * 0.55f);
				const glm::vec3 across(side.x, 0.0f, side.y);
				quad(a + across, b + across, b - across, a - across, glm::vec3(0.0f, 1.0f, 0.0f), lakeColour);
			}
		}

		// Falls: a sheet each, both sides
		for (const TerrainWater::Fall& fall : water.Falls)
		{
			const glm::vec3 topLeft = fall.Top - fall.Across;
			const glm::vec3 topRight = fall.Top + fall.Across;
			const glm::vec3 drop(0.0f, fall.Bottom - fall.Top.y, 0.0f);
			const glm::vec3 facing = glm::normalize(glm::cross(fall.Across, glm::vec3(0.0f, 1.0f, 0.0f)));
			quad(topLeft, topLeft + drop, topRight + drop, topRight, facing, fallColour);
			quad(topRight, topRight + drop, topLeft + drop, topLeft, -facing, fallColour);
		}
		return mesh;
	}

	namespace Detail
	{
		// A working-out on a worker: what it was made from, and what it made, taken when it is over
		struct TerrainWaterWork
		{
			std::shared_ptr<const SdfTerrainComponent> From;
			uint64_t Shape = 0;
			JobHandle Job;
			std::shared_ptr<const TerrainWater> Water;
			std::shared_ptr<const MeshData> Mesh;
			double Milliseconds = 0.0;
			bool bTaken = false;
		};
	}

	void UpdateTerrainWater(Registry& world, JobSystem& jobs)
	{
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		std::vector<Entity> gone;
		world.Each<TerrainWaterComponent>([&](Entity entity, TerrainWaterComponent&)
			{
				if (!world.Has<SdfTerrainComponent>(entity) || !UsesTerrainWorldPlan(world.Get<SdfTerrainComponent>(entity)))
				{
					gone.push_back(entity);
				}
			});
		for (const Entity entity : gone)
		{
			world.Remove<TerrainWaterComponent>(entity);
		}

		for (const Entity entity : terrains)
		{
			SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(entity);
			if (!UsesTerrainWorldPlan(terrain))
			{
				continue;
			}
			if (!world.Has<TerrainWaterComponent>(entity))
			{
				world.Add<TerrainWaterComponent>(entity, TerrainWaterComponent{});
			}
			TerrainWaterComponent& component = world.Get<TerrainWaterComponent>(entity);
			std::shared_ptr<Detail::TerrainWaterWork>& work = component.Work;

			// Done: what it made goes in
			if (work && !work->bTaken && work->Job.IsOver())
			{
				work->bTaken = true;
				if (work->Water)
				{
					component.Water = work->Water;
					SetRuntimeMesh(component.Mesh, work->Mesh);
					++component.Made;
					component.LastMilliseconds = work->Milliseconds;
				}
			}
			if (work && !work->bTaken)
			{
				continue;
			}

			// Changed since: the layers or plan (all of it again), or the edits (the ground again only
			// where those that differ reach; bakes change nothing it finds)
			const uint64_t shape = HashSdfTerrainShape(terrain);
			const bool bSameShape = work && work->Shape == shape && component.Water;
			const size_t same = bSameShape ? CountSameTerrainEdits(terrain, *work->From) : 0;
			if (bSameShape && same == terrain.Edits.size() && same == work->From->Edits.size())
			{
				continue;
			}
			glm::vec3 changedMin(std::numeric_limits<float>::max());
			glm::vec3 changedMax(-std::numeric_limits<float>::max());
			bool bChanged = !bSameShape;
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
					changedMin = glm::min(changedMin, low);
					changedMax = glm::max(changedMax, high);
					bChanged = true;
				}
			};
			if (bSameShape)
			{
				widen(terrain.Edits);
				widen(work->From->Edits);
			}
			auto next = std::make_shared<Detail::TerrainWaterWork>();
			next->From = std::make_shared<SdfTerrainComponent>(terrain);
			next->Shape = shape;
			if (!bChanged)
			{
				// Only bakes came or went: the same water
				next->Water = component.Water;
				next->bTaken = true;
				work = next;
				continue;
			}
			const std::shared_ptr<const TerrainWater> earlier = bSameShape ? component.Water : nullptr;
			const bool bPart = earlier != nullptr;
			JobSystem* workers = &jobs;
			next->Job = jobs.Schedule(
				[making = next, earlier, bPart, changedMin, changedMax, workers]
				{
					const auto start = std::chrono::steady_clock::now();
					auto water = std::make_shared<TerrainWater>(MakeTerrainWater(*making->From, earlier.get(), bPart ? &changedMin : nullptr, bPart ? &changedMax : nullptr, workers));
					making->Mesh = std::make_shared<MeshData>(MakeTerrainWaterMesh(*water));
					making->Water = std::move(water);
					making->Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				},
				EJobPriority::Normal);
			work = next;
		}
	}

	std::vector<InspectedValue> DescribeTerrainWater(const TerrainWaterComponent& component)
	{
		if (!component.Water)
		{
			return { { "Water", "being worked out" } };
		}
		const TerrainWater& water = *component.Water;
		size_t sea = 0;
		size_t lakes = 0;
		for (const TerrainWater::EBody body : water.Body)
		{
			sea += body == TerrainWater::EBody::Sea ? 1 : 0;
			lakes += body == TerrainWater::EBody::Lake ? 1 : 0;
		}
		return { { "Cells", std::format("{} sea, {} lakes and pools, of {} x {} of {} m", sea, lakes, water.Cells, water.Cells, TerrainWater::CellSize) },
			{ "Rivers", std::format("{} stretches, {} falls", water.Rivers.size(), water.Falls.size()) },
			{ "Worked Out", std::format("{} times, the last in {:.1f} ms", component.Made, component.LastMilliseconds) } };
	}
}

#include "NyxPCH.h"
#include "TerrainTraversal.h"

#include "Profiler.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainPlacement.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Nyx
{
	namespace
	{
		// The most columns a side; a larger world's are farther apart
		constexpr int MostColumns = 2048;
		// The column's samples a metre apart, 64 at a time; the floor's crossing halved to a couple of
		// centimetres
		constexpr float ColumnStep = 1.0f;
		constexpr int ColumnBatch = 64;
		constexpr int Halvings = 6;
		// A column's floors at most
		constexpr int MostFloors = 8;
		// How deep under water a floor is wet
		constexpr float WetDepth = 0.5f;
		// The cells the approaches are counted over
		constexpr float ApproachCell = 16.0f;

		constexpr std::array<glm::ivec2, 8> Around = { glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1), glm::ivec2(1, 1), glm::ivec2(-1, 1),
			glm::ivec2(1, -1), glm::ivec2(-1, -1) };

		template <typename T>
		void AddToHash(uint64_t& hash, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			for (size_t index = 0; index < sizeof(T); ++index)
			{
				hash = (hash ^ uint64_t(bytes[index])) * 0x100000001B3ull;
			}
		}

		void AddTextToHash(uint64_t& hash, const std::string& text)
		{
			AddToHash(hash, text.size());
			for (const char character : text)
			{
				AddToHash(hash, character);
			}
		}

		// The floors down a column from `top` to `bottom`: where the field goes from outside to inside
		// going down with Headroom of outside above, highest first
		void FindFloors(const SdfTerrainComponent& terrain, const glm::vec2& point, float top, float bottom, float headroom, std::vector<float>& outFloors)
		{
			outFloors.clear();
			std::array<float, ColumnBatch> x;
			std::array<float, ColumnBatch> y;
			std::array<float, ColumnBatch> z;
			std::array<float, ColumnBatch> values;
			x.fill(point.x);
			z.fill(point.y);
			// The last sample's height and whether it was outside; where the air above it starts
			float above = top;
			bool bOutside = SampleTerrainValue(terrain, glm::vec3(point.x, top, point.y)) > 0.0f;
			float airTop = bOutside ? std::numeric_limits<float>::max() : top;
			for (float from = top; from > bottom && int(outFloors.size()) < MostFloors; from -= ColumnStep * float(ColumnBatch))
			{
				for (int n = 0; n < ColumnBatch; ++n)
				{
					y[size_t(n)] = from - ColumnStep * float(n + 1);
				}
				SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), size_t(ColumnBatch), values.data());
				for (int n = 0; n < ColumnBatch && y[size_t(n)] >= bottom; ++n)
				{
					const bool bNowOutside = values[size_t(n)] > 0.0f;
					if (bOutside != bNowOutside)
					{
						// The crossing between them, halved: a floor going in, a ceiling coming out
						float high = above;
						float low = y[size_t(n)];
						for (int halving = 0; halving < Halvings; ++halving)
						{
							const float middle = (high + low) * 0.5f;
							((SampleTerrainValue(terrain, glm::vec3(point.x, middle, point.y)) > 0.0f) == bOutside ? high : low) = middle;
						}
						if (!bOutside)
						{
							airTop = low;
						}
						else if (airTop - high >= headroom && int(outFloors.size()) < MostFloors)
						{
							outFloors.push_back((high + low) * 0.5f);
						}
					}
					bOutside = bNowOutside;
					above = y[size_t(n)];
				}
			}
		}

		// Union-find over floors, for the approaches' cells
		uint32_t FindRoot(std::vector<uint32_t>& parents, uint32_t node)
		{
			while (parents[node] != node)
			{
				parents[node] = parents[parents[node]];
				node = parents[node];
			}
			return node;
		}

		float EdgeCost(const TerrainTraversal& traversal, uint32_t from, uint32_t to, uint8_t tag)
		{
			const float length = glm::length(traversal.PlaceOf(to) - traversal.PlaceOf(from));
			return length * ((tag & TerrainSwim) != 0 ? 3.0f : ((tag & (TerrainClimb | TerrainGap)) != 0 ? 2.0f : 1.0f));
		}
	}

	uint32_t TerrainTraversal::ColumnOf(uint32_t floor) const
	{
		return uint32_t(std::upper_bound(ColumnFirst.begin(), ColumnFirst.end(), floor) - ColumnFirst.begin()) - 1u;
	}

	glm::vec3 TerrainTraversal::PlaceOf(uint32_t floor) const
	{
		const uint32_t column = ColumnOf(floor);
		const glm::vec2 point = PointOf(int(column % uint32_t(Columns)), int(column / uint32_t(Columns)));
		return glm::vec3(point.x, FloorHeights[floor], point.y);
	}

	TerrainTraversal BuildTerrainTraversal(const SdfTerrainComponent& terrain, const TerrainWater* water, JobSystem* jobs)
	{
		NYX_PROFILE_FUNCTION();
		const auto start = std::chrono::steady_clock::now();
		TerrainTraversal traversal;
		traversal.Values = terrain.Traversal;
		SdfTerrainTraversal& values = traversal.Values;
		const std::shared_ptr<const TerrainWorldPlan> plan = UsesTerrainWorldPlan(terrain) ? GetTerrainWorldPlan(terrain) : nullptr;
		if (!plan || plan->Nodes < 2)
		{
			traversal.ColumnFirst.assign(1, 0);
			traversal.EdgeFirst.assign(1, 0);
			return traversal;
		}
		const float span = float(plan->Nodes - 1) * TerrainWorldPlan::CellSize;
		values.Spacing = std::max({ values.Spacing, 0.5f, span / float(MostColumns - 1) });
		values.Headroom = std::max(values.Headroom, 0.1f);
		traversal.Columns = int(std::floor(span / values.Spacing)) + 1;
		traversal.Origin = plan->Origin;
		const int columns = traversal.Columns;
		const size_t count = size_t(columns) * size_t(columns);

		// Each column's floors, row by row on the workers, then put together in order
		const SdfTerrainWorldPlan& made = plan->Made;
		const float lowest = -made.Thickness - std::fabs(made.UndersideDepth) - std::fabs(made.RiftDepth) - 50.0f;
		std::vector<std::vector<float>> rows = std::vector<std::vector<float>>(size_t(columns));
		std::vector<std::vector<uint8_t>> rowCounts = std::vector<std::vector<uint8_t>>(size_t(columns));
		auto findRow = [&](size_t row)
		{
			std::vector<float> floors;
			std::vector<float>& heights = rows[row];
			std::vector<uint8_t>& counts = rowCounts[row];
			counts.assign(size_t(columns), 0);
			for (int i = 0; i < columns; ++i)
			{
				const glm::vec2 point = traversal.PointOf(i, int(row));
				if (plan->EdgeValue(point.x, point.y) > values.Spacing)
				{
					continue;
				}
				const TerrainWorldPlan::Bounds bounds = plan->GetRange(point - TerrainWorldPlan::CellSize, point + TerrainWorldPlan::CellSize);
				const float top = bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f + values.Headroom + 4.0f;
				FindFloors(terrain, point, top, bounds.LowHeight + lowest, values.Headroom, floors);
				heights.insert(heights.end(), floors.begin(), floors.end());
				counts[size_t(i)] = uint8_t(floors.size());
			}
		};
		if (jobs != nullptr)
		{
			jobs->ParallelFor(size_t(columns), findRow, EJobPriority::Normal);
		}
		else
		{
			for (size_t row = 0; row < size_t(columns); ++row)
			{
				findRow(row);
			}
		}
		traversal.ColumnFirst.resize(count + 1);
		uint32_t next = 0;
		for (int j = 0; j < columns; ++j)
		{
			for (int i = 0; i < columns; ++i)
			{
				traversal.ColumnFirst[traversal.IndexOf(i, j)] = next;
				next += rowCounts[size_t(j)][size_t(i)];
			}
			traversal.FloorHeights.insert(traversal.FloorHeights.end(), rows[size_t(j)].begin(), rows[size_t(j)].end());
			rows[size_t(j)].clear();
			rows[size_t(j)].shrink_to_fit();
		}
		traversal.ColumnFirst[count] = next;

		// Under water: where the water over a floor's cell is higher than it
		traversal.Wet.assign(traversal.Floors(), 0);
		if (water != nullptr && water->Cells > 0)
		{
			for (size_t column = 0; column < count; ++column)
			{
				const glm::vec2 point = traversal.PointOf(int(column % size_t(columns)), int(column / size_t(columns)));
				const glm::ivec2 cell = water->CellOf(point);
				const size_t index = water->IndexOf(cell.x, cell.y);
				if (water->Body[index] == TerrainWater::EBody::None)
				{
					continue;
				}
				for (uint32_t floor = traversal.ColumnFirst[column]; floor < traversal.ColumnFirst[column + 1]; ++floor)
				{
					traversal.Wet[floor] = traversal.FloorHeights[floor] < water->Level[index] - WetDepth ? 1 : 0;
				}
			}
		}

		// Each floor's edges to the columns round it, row by row on the workers
		const float walkSlope = std::tan(glm::radians(std::clamp(values.WalkSlope, 0.0f, 89.0f)));
		std::vector<std::vector<uint32_t>> rowTargets = std::vector<std::vector<uint32_t>>(size_t(columns));
		std::vector<std::vector<uint8_t>> rowTags = std::vector<std::vector<uint8_t>>(size_t(columns));
		std::vector<std::vector<uint32_t>> rowEdgeCounts = std::vector<std::vector<uint32_t>>(size_t(columns));
		auto floorsOf = [&](int i, int j, uint32_t& first, uint32_t& last)
		{
			const size_t column = traversal.IndexOf(i, j);
			first = traversal.ColumnFirst[column];
			last = traversal.ColumnFirst[column + 1];
		};
		auto linkRow = [&](size_t row)
		{
			const int j = int(row);
			std::vector<uint32_t>& targets = rowTargets[row];
			std::vector<uint8_t>& tags = rowTags[row];
			std::vector<uint32_t>& counts = rowEdgeCounts[row];
			for (int i = 0; i < columns; ++i)
			{
				uint32_t first = 0;
				uint32_t last = 0;
				floorsOf(i, j, first, last);
				for (uint32_t floor = first; floor < last; ++floor)
				{
					const size_t before = targets.size();
					const float height = traversal.FloorHeights[floor];
					const glm::vec2 point = traversal.PointOf(i, j);
					for (const glm::ivec2& step : Around)
					{
						const int ni = i + step.x;
						const int nj = j + step.y;
						if (ni < 0 || nj < 0 || ni >= columns || nj >= columns)
						{
							continue;
						}
						const float across = values.Spacing * (step.x != 0 && step.y != 0 ? 1.41421356f : 1.0f);
						const float walkRise = values.StepHeight + across * walkSlope;
						uint32_t otherFirst = 0;
						uint32_t otherLast = 0;
						floorsOf(ni, nj, otherFirst, otherLast);
						// Ground under the middle of the way between two floors, about as low as the lower
						const glm::vec2 middle = (point + traversal.PointOf(ni, nj)) * 0.5f;
						auto supported = [&](float lower) { return SampleTerrainValue(terrain, glm::vec3(middle.x, lower - 0.5f, middle.y)) <= 0.0f; };
						bool bWalked = false;
						for (uint32_t other = otherFirst; other < otherLast; ++other)
						{
							const float rise = traversal.FloorHeights[other] - height;
							const bool bDrop = rise < -walkRise && -rise <= values.DropHeight;
							uint8_t tag = 0;
							if (std::fabs(rise) <= walkRise || bDrop)
							{
								tag = TerrainWalk;
							}
							else if (rise > 0.0f && rise <= values.ClimbHeight)
							{
								tag = TerrainClimb;
							}
							const bool bWet = traversal.Wet[floor] != 0 || traversal.Wet[other] != 0;
							if (bWet)
							{
								tag = tag != 0 || (traversal.Wet[floor] != 0 && traversal.Wet[other] != 0) ? uint8_t(TerrainSwim) : uint8_t(0);
							}
							// Room between them, over the higher; ground on the way, but over a drop's edge or
							// in water
							if (tag == 0 || SampleTerrainValue(terrain, glm::vec3(middle.x, std::max(height, height + rise) + values.Headroom * 0.5f, middle.y)) <= 0.0f
								|| (!bDrop && !bWet && !supported(std::min(height, height + rise))))
							{
								continue;
							}
							targets.push_back(other);
							tags.push_back(tag);
							bWalked = bWalked || tag == TerrainWalk;
						}

						// A gap: no ground within walking of this floor on the way (no floor in the columns
						// between, or none under the middle of the way to the next), ground about as high
						// past it within a jump, the air over the way free
						if (bWalked || values.GapWidth < across)
						{
							continue;
						}
						const int farthest = int(std::floor(values.GapWidth / across));
						bool bFound = false;
						for (int k = 1; k <= farthest && !bFound; ++k)
						{
							const int ti = i + step.x * k;
							const int tj = j + step.y * k;
							if (ti < 0 || tj < 0 || ti >= columns || tj >= columns)
							{
								break;
							}
							bool bOpen = k > 1 || !supported(height);
							for (int m = 1; m < k && bOpen; ++m)
							{
								uint32_t gapFirst = 0;
								uint32_t gapLast = 0;
								floorsOf(i + step.x * m, j + step.y * m, gapFirst, gapLast);
								for (uint32_t gap = gapFirst; gap < gapLast && bOpen; ++gap)
								{
									bOpen = traversal.FloorHeights[gap] < height - values.StepHeight - 1.0f || traversal.FloorHeights[gap] > height + values.Headroom;
								}
								const glm::vec2 over = traversal.PointOf(i + step.x * m, j + step.y * m);
								bOpen = bOpen && SampleTerrainValue(terrain, glm::vec3(over.x, height + values.Headroom * 0.5f, over.y)) > 0.0f;
							}
							if (!bOpen)
							{
								break;
							}
							uint32_t farFirst = 0;
							uint32_t farLast = 0;
							floorsOf(ti, tj, farFirst, farLast);
							for (uint32_t other = farFirst; other < farLast; ++other)
							{
								if (std::fabs(traversal.FloorHeights[other] - height) <= values.StepHeight + 1.0f)
								{
									targets.push_back(other);
									tags.push_back(TerrainGap);
									bFound = true;
								}
							}
						}
					}
					counts.push_back(uint32_t(targets.size() - before));
				}
			}
		};
		if (jobs != nullptr)
		{
			jobs->ParallelFor(size_t(columns), linkRow, EJobPriority::Normal);
		}
		else
		{
			for (size_t row = 0; row < size_t(columns); ++row)
			{
				linkRow(row);
			}
		}
		traversal.EdgeFirst.reserve(traversal.Floors() + 1);
		uint32_t edges = 0;
		for (size_t row = 0; row < size_t(columns); ++row)
		{
			for (const uint32_t edgeCount : rowEdgeCounts[row])
			{
				traversal.EdgeFirst.push_back(edges);
				edges += edgeCount;
			}
			traversal.EdgeTargets.insert(traversal.EdgeTargets.end(), rowTargets[row].begin(), rowTargets[row].end());
			traversal.EdgeTags.insert(traversal.EdgeTags.end(), rowTags[row].begin(), rowTags[row].end());
		}
		traversal.EdgeFirst.push_back(edges);
		traversal.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return traversal;
	}

	std::optional<uint32_t> FindTerrainFloor(const TerrainTraversal& traversal, const glm::vec3& point, float reach)
	{
		if (traversal.Columns == 0)
		{
			return std::nullopt;
		}
		const glm::vec2 at = (glm::vec2(point.x, point.z) - traversal.Origin) / traversal.Values.Spacing;
		const int ci = int(std::round(at.x));
		const int cj = int(std::round(at.y));
		const int ring = int(std::ceil(reach / traversal.Values.Spacing));
		std::optional<uint32_t> best;
		float bestCost = std::numeric_limits<float>::max();
		for (int dj = -ring; dj <= ring; ++dj)
		{
			for (int di = -ring; di <= ring; ++di)
			{
				const int i = ci + di;
				const int j = cj + dj;
				if (i < 0 || j < 0 || i >= traversal.Columns || j >= traversal.Columns)
				{
					continue;
				}
				const size_t column = traversal.IndexOf(i, j);
				const glm::vec2 place = traversal.PointOf(i, j);
				const float along = glm::length(place - glm::vec2(point.x, point.z));
				if (along > reach)
				{
					continue;
				}
				for (uint32_t floor = traversal.ColumnFirst[column]; floor < traversal.ColumnFirst[column + 1]; ++floor)
				{
					const float cost = along + std::fabs(traversal.FloorHeights[floor] - point.y) * 2.0f;
					if (cost < bestCost)
					{
						bestCost = cost;
						best = floor;
					}
				}
			}
		}
		return best;
	}

	std::optional<TerrainRoute> FindTerrainRoute(const TerrainTraversal& traversal, uint32_t from, uint32_t to, uint8_t tags)
	{
		if (from >= traversal.Floors() || to >= traversal.Floors())
		{
			return std::nullopt;
		}
		const glm::vec3 goal = traversal.PlaceOf(to);
		std::unordered_map<uint32_t, float> costs;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint8_t>> cameFrom;
		using Entry = std::pair<float, uint32_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
		costs[from] = 0.0f;
		open.emplace(glm::length(goal - traversal.PlaceOf(from)), from);
		while (!open.empty())
		{
			const auto [estimate, floor] = open.top();
			open.pop();
			if (floor == to)
			{
				TerrainRoute route;
				route.Length = costs[to];
				for (uint32_t at = to;;)
				{
					route.Floors.push_back(at);
					const auto back = cameFrom.find(at);
					if (back == cameFrom.end())
					{
						break;
					}
					route.Tags |= back->second.second;
					at = back->second.first;
				}
				std::reverse(route.Floors.begin(), route.Floors.end());
				return route;
			}
			const float cost = costs[floor];
			if (estimate - glm::length(goal - traversal.PlaceOf(floor)) > cost + 1e-3f)
			{
				continue;
			}
			for (uint32_t edge = traversal.EdgeFirst[floor]; edge < traversal.EdgeFirst[floor + 1]; ++edge)
			{
				const uint8_t tag = traversal.EdgeTags[edge];
				if ((tag & tags) == 0)
				{
					continue;
				}
				const uint32_t target = traversal.EdgeTargets[edge];
				const float through = cost + EdgeCost(traversal, floor, target, tag);
				const auto known = costs.find(target);
				if (known != costs.end() && known->second <= through)
				{
					continue;
				}
				costs[target] = through;
				cameFrom[target] = { floor, tag };
				open.emplace(through + glm::length(goal - traversal.PlaceOf(target)), target);
			}
		}
		return std::nullopt;
	}

	std::vector<uint8_t> GetTerrainReach(const TerrainTraversal& traversal, uint32_t from)
	{
		std::vector<uint8_t> reach(traversal.Floors(), 0);
		if (from >= traversal.Floors())
		{
			return reach;
		}
		constexpr std::array<uint8_t, 4> Passes = { TerrainWalk, TerrainWalk | TerrainSwim, TerrainWalk | TerrainSwim | TerrainClimb, TerrainAllTags };
		std::vector<uint32_t> open;
		for (size_t pass = 0; pass < Passes.size(); ++pass)
		{
			// From everything reached so far, with more allowed
			open.clear();
			for (uint32_t floor = 0; floor < uint32_t(reach.size()); ++floor)
			{
				if (reach[floor] != 0 || floor == from)
				{
					open.push_back(floor);
				}
			}
			if (reach[from] == 0)
			{
				reach[from] = 1;
			}
			while (!open.empty())
			{
				const uint32_t floor = open.back();
				open.pop_back();
				for (uint32_t edge = traversal.EdgeFirst[floor]; edge < traversal.EdgeFirst[floor + 1]; ++edge)
				{
					const uint32_t target = traversal.EdgeTargets[edge];
					if ((traversal.EdgeTags[edge] & Passes[pass]) != 0 && reach[target] == 0)
					{
						reach[target] = uint8_t(pass + 1);
						open.push_back(target);
					}
				}
			}
		}
		return reach;
	}

	uint32_t CountTerrainApproaches(const TerrainTraversal& traversal, uint32_t from, const glm::vec3& to, float radius, uint8_t tags, uint32_t most)
	{
		if (from >= traversal.Floors() || most == 0)
		{
			return 0;
		}
		// The cells: a floor's 16 m cell, and of the floors in a cell those joined within it one node
		const size_t floors = traversal.Floors();
		const int cellColumns = std::max(int(std::round(ApproachCell / traversal.Values.Spacing)), 1);
		std::vector<uint32_t> cellOf(floors);
		for (uint32_t floor = 0; floor < uint32_t(floors); ++floor)
		{
			const uint32_t column = traversal.ColumnOf(floor);
			const int i = int(column % uint32_t(traversal.Columns)) / cellColumns;
			const int j = int(column / uint32_t(traversal.Columns)) / cellColumns;
			cellOf[floor] = uint32_t(i) + uint32_t(j) * uint32_t((traversal.Columns + cellColumns - 1) / cellColumns);
		}
		std::vector<uint32_t> parents(floors);
		for (uint32_t floor = 0; floor < uint32_t(floors); ++floor)
		{
			parents[floor] = floor;
		}
		for (uint32_t floor = 0; floor < uint32_t(floors); ++floor)
		{
			for (uint32_t edge = traversal.EdgeFirst[floor]; edge < traversal.EdgeFirst[floor + 1]; ++edge)
			{
				const uint32_t target = traversal.EdgeTargets[edge];
				if ((traversal.EdgeTags[edge] & tags) != 0 && cellOf[target] == cellOf[floor])
				{
					parents[FindRoot(parents, floor)] = FindRoot(parents, target);
				}
			}
		}
		// Nodes numbered; the place's floors one node, the sink
		std::unordered_map<uint32_t, uint32_t> nodeOfRoot;
		std::vector<uint32_t> nodeOf(floors);
		const glm::vec2 goal(to.x, to.z);
		constexpr uint32_t Sink = 0;
		uint32_t nodes = 1;
		for (uint32_t floor = 0; floor < uint32_t(floors); ++floor)
		{
			const glm::vec3 place = traversal.PlaceOf(floor);
			if (glm::length(glm::vec2(place.x, place.z) - goal) <= radius && std::fabs(place.y - to.y) <= traversal.Values.ClimbHeight + traversal.Values.Headroom)
			{
				nodeOf[floor] = Sink;
				continue;
			}
			const uint32_t root = FindRoot(parents, floor);
			const auto [it, bAdded] = nodeOfRoot.try_emplace(root, nodes);
			nodes += bAdded ? 1 : 0;
			nodeOf[floor] = it->second;
		}
		const uint32_t source = nodeOf[from];
		if (source == Sink)
		{
			return most;
		}

		// Vertex-disjoint ways: each node but the source and sink split in two, in and out, joined by a
		// capacity of one; the cells' edges between them, unbounded
		struct Arc
		{
			uint32_t To;
			int32_t Capacity;
		};
		std::vector<std::vector<uint32_t>> arcsOf(size_t(nodes) * 2);
		std::vector<Arc> arcs;
		auto inOf = [](uint32_t node) { return node * 2; };
		auto outOf = [&](uint32_t node) { return node == Sink || node == source ? node * 2 : node * 2 + 1; };
		auto addArc = [&](uint32_t a, uint32_t b, int32_t capacity)
		{
			arcsOf[a].push_back(uint32_t(arcs.size()));
			arcs.push_back({ b, capacity });
			arcsOf[b].push_back(uint32_t(arcs.size()));
			arcs.push_back({ a, 0 });
		};
		for (uint32_t node = 1; node < nodes; ++node)
		{
			if (node != source)
			{
				addArc(inOf(node), outOf(node), 1);
			}
		}
		std::unordered_map<uint64_t, uint8_t> linked;
		for (uint32_t floor = 0; floor < uint32_t(floors); ++floor)
		{
			for (uint32_t edge = traversal.EdgeFirst[floor]; edge < traversal.EdgeFirst[floor + 1]; ++edge)
			{
				const uint32_t a = nodeOf[floor];
				const uint32_t b = nodeOf[traversal.EdgeTargets[edge]];
				if ((traversal.EdgeTags[edge] & tags) == 0 || a == b || a == Sink || b == source)
				{
					continue;
				}
				if (linked.try_emplace((uint64_t(a) << 32) | b, 1).second)
				{
					addArc(outOf(a), inOf(b), 1 << 20);
				}
			}
		}

		// Ways found one at a time, by breadth first through what is left
		uint32_t ways = 0;
		std::vector<int64_t> cameBy(arcsOf.size());
		while (ways < most)
		{
			std::fill(cameBy.begin(), cameBy.end(), -1);
			std::vector<uint32_t> open{ outOf(source) };
			cameBy[outOf(source)] = -2;
			for (size_t at = 0; at < open.size() && cameBy[inOf(Sink)] == -1; ++at)
			{
				for (const uint32_t arc : arcsOf[open[at]])
				{
					if (arcs[arc].Capacity > 0 && cameBy[arcs[arc].To] == -1)
					{
						cameBy[arcs[arc].To] = arc;
						open.push_back(arcs[arc].To);
					}
				}
			}
			if (cameBy[inOf(Sink)] == -1)
			{
				break;
			}
			for (uint32_t at = inOf(Sink); cameBy[at] >= 0;)
			{
				const uint32_t arc = uint32_t(cameBy[at]);
				arcs[arc].Capacity -= 1;
				arcs[arc ^ 1u].Capacity += 1;
				at = arcs[arc ^ 1u].To;
			}
			++ways;
		}
		return ways;
	}

	std::vector<TerrainRouteResult> CheckTerrainRoutes(const SdfTerrainComponent& terrain, const TerrainPlacement& placement, const TerrainTraversal& traversal)
	{
		std::vector<TerrainRouteResult> results;
		for (const SdfTerrainRouteCheck& check : terrain.RouteChecks)
		{
			TerrainRouteResult& result = results.emplace_back();
			if (!check.bEnabled)
			{
				result.bPassed = true;
				result.Why = "off";
				continue;
			}
			const std::optional<uint32_t> fromRule = FindTerrainPlaceRule(terrain, check.From);
			const std::optional<uint32_t> toRule = FindTerrainPlaceRule(terrain, check.To);
			if (!fromRule || !toRule)
			{
				result.Why = std::format("no Place Rule named {}", !fromRule ? check.From : check.To);
				continue;
			}
			const TerrainPlace* start = nullptr;
			for (const TerrainPlace& place : placement.Places)
			{
				if (place.Rule == *fromRule)
				{
					start = &place;
					break;
				}
			}
			const std::optional<uint32_t> from = start ? FindTerrainFloor(traversal, start->Position) : std::nullopt;
			if (!from)
			{
				result.Why = std::format("{} has no place on a floor to start from", check.From);
				continue;
			}
			const uint8_t tags = uint8_t(TerrainWalk | (check.bClimb ? TerrainClimb : 0) | (check.bGap ? TerrainGap : 0) | (check.bSwim ? TerrainSwim : 0));
			const float radius = terrain.PlaceRules[*toRule].Radius + traversal.Values.Spacing * 2.0f;
			result.FewestApproaches = UINT32_MAX;
			for (const TerrainPlace& place : placement.Places)
			{
				if (place.Rule != *toRule)
				{
					continue;
				}
				++result.Places;
				const std::optional<uint32_t> to = FindTerrainFloor(traversal, place.Position);
				if (!to || !FindTerrainRoute(traversal, *from, *to, tags))
				{
					result.FewestApproaches = 0;
					continue;
				}
				++result.Reached;
				result.FewestApproaches = std::min(result.FewestApproaches, CountTerrainApproaches(traversal, *from, place.Position, radius, tags, std::max(check.Approaches, 1u)));
			}
			result.FewestApproaches = result.Places == 0 ? 0 : result.FewestApproaches;
			result.bPassed = result.Places > 0 && result.Reached == result.Places && result.FewestApproaches >= check.Approaches;
			result.Why = result.Places == 0 ? std::format("{} has no places", check.To)
											: std::format("{} of {} reached, the fewest ways {}", result.Reached, result.Places, result.FewestApproaches);
		}
		return results;
	}

	namespace Detail
	{
		// A building on a worker: what it was built from, and what it built, taken when it is over
		struct TerrainTraversalWork
		{
			std::shared_ptr<const SdfTerrainComponent> From;
			std::shared_ptr<const TerrainPlacement> Placement;
			uint64_t Key = 0;
			JobHandle Job;
			std::shared_ptr<const TerrainTraversal> Traversal;
			std::vector<TerrainRouteResult> Routes;
			bool bTaken = false;
		};
	}

	void UpdateTerrainTraversal(Registry& world, JobSystem& jobs)
	{
		NYX_PROFILE_FUNCTION();
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		std::vector<Entity> gone;
		world.Each<TerrainTraversalComponent>([&](Entity entity, TerrainTraversalComponent&)
			{
				if (!world.Has<SdfTerrainComponent>(entity) || world.Get<SdfTerrainComponent>(entity).RouteChecks.empty())
				{
					gone.push_back(entity);
				}
			});
		for (const Entity entity : gone)
		{
			world.Remove<TerrainTraversalComponent>(entity);
		}

		for (const Entity entity : terrains)
		{
			const SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(entity);
			// After the places they check
			if (terrain.RouteChecks.empty() || !world.Has<TerrainPlacesComponent>(entity) || !world.Get<TerrainPlacesComponent>(entity).Placement)
			{
				continue;
			}
			const TerrainPlacesComponent& places = world.Get<TerrainPlacesComponent>(entity);
			if (!world.Has<TerrainTraversalComponent>(entity))
			{
				world.Add<TerrainTraversalComponent>(entity, TerrainTraversalComponent{});
			}
			TerrainTraversalComponent& component = world.Get<TerrainTraversalComponent>(entity);
			std::shared_ptr<Detail::TerrainTraversalWork>& work = component.Work;
			if (work && !work->bTaken && work->Job.IsOver())
			{
				work->bTaken = true;
				component.Traversal = work->Traversal;
				component.Routes = work->Routes;
				component.Key = work->Key;
				++component.Made;
			}
			if (work && !work->bTaken)
			{
				continue;
			}

			// The shape (layers, plan, seed), the values and checks, and the places found
			uint64_t key = HashSdfTerrainShape(terrain);
			AddToHash(key, places.Key);
			AddToHash(key, terrain.Traversal);
			for (const SdfTerrainRouteCheck& check : terrain.RouteChecks)
			{
				AddToHash(key, check.bEnabled);
				AddTextToHash(key, check.From);
				AddTextToHash(key, check.To);
				AddToHash(key, check.bClimb);
				AddToHash(key, check.bGap);
				AddToHash(key, check.bSwim);
				AddToHash(key, check.Approaches);
			}
			if (work && work->Key == key)
			{
				continue;
			}
			// The graph only when what it is built from changed; the checks again either way
			const bool bSameGraph = component.Traversal && work && work->From && HashSdfTerrainShape(*work->From) == HashSdfTerrainShape(terrain)
				&& std::memcmp(&work->From->Traversal, &terrain.Traversal, sizeof(SdfTerrainTraversal)) == 0;
			auto next = std::make_shared<Detail::TerrainTraversalWork>();
			auto from = std::make_shared<SdfTerrainComponent>(terrain);
			from->Edits.clear();
			next->From = std::move(from);
			next->Placement = places.Placement;
			next->Key = key;
			const std::shared_ptr<const TerrainTraversal> earlier = bSameGraph ? component.Traversal : nullptr;
			JobSystem* workers = &jobs;
			next->Job = jobs.Schedule(
				[building = next, earlier, workers]
				{
					if (earlier)
					{
						building->Traversal = earlier;
					}
					else
					{
						const TerrainWater water = MakeTerrainWater(*building->From, nullptr, nullptr, nullptr, workers);
						building->Traversal = std::make_shared<TerrainTraversal>(BuildTerrainTraversal(*building->From, &water, workers));
					}
					building->Routes = CheckTerrainRoutes(*building->From, *building->Placement, *building->Traversal);
				},
				EJobPriority::Normal);
			work = next;
		}
	}

	std::vector<InspectedValue> DescribeTerrainTraversal(const TerrainTraversalComponent& component)
	{
		if (!component.Traversal)
		{
			return { { "Traversal", "being built" } };
		}
		const TerrainTraversal& traversal = *component.Traversal;
		std::vector<InspectedValue> values;
		values.push_back({ "Graph", std::format("{} x {} columns {:.1f} m apart, {} floors, {} edges, built in {:.0f} ms", traversal.Columns, traversal.Columns,
										traversal.Values.Spacing, traversal.Floors(), traversal.EdgeTargets.size(), traversal.Milliseconds) });
		for (size_t index = 0; index < component.Routes.size(); ++index)
		{
			const TerrainRouteResult& result = component.Routes[index];
			values.push_back({ std::format("Check {}", index), std::format("{}: {}", result.bPassed ? "passed" : "failed", result.Why) });
		}
		return values;
	}
}

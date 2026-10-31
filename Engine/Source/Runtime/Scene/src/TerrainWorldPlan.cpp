#include "NyxPCH.h"
#include "TerrainWorldPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <queue>
#include <utility>

// Every operation as written, never fused into a multiply-add: a plan is the same bits on every
// machine
#pragma fp_contract(off)

namespace Nyx
{
	namespace
	{
		constexpr float Cell = TerrainWorldPlan::CellSize;

		// Which of the plan's noises: each its own seed
		enum class EPlanNoise : uint32_t
		{
			Edge,
			Sea,
			Rough,
			RiftBendX,
			RiftBendZ,
			Pins,
			Meander,
			Lakes,
			Tunnels
		};

		uint32_t Mix(uint32_t value)
		{
			value ^= value >> 16;
			value *= 0x7FEB352Du;
			value ^= value >> 15;
			value *= 0x846CA68Bu;
			value ^= value >> 16;
			return value;
		}

		// A number from 0 to 1, below 1, the same on every machine
		float Random(uint32_t seed, uint32_t what, uint32_t index)
		{
			return float(Mix(seed * 0x9E3779B9u ^ Mix(what * 0x85EBCA77u + index)) >> 8) * (1.0f / 16777216.0f);
		}

		FractalNoise3D PlanNoise(uint32_t seed, EPlanNoise role, float frequency, uint32_t octaves)
		{
			FractalNoise3D noise;
			noise.Seed = Mix(seed * 0x27D4EB2Fu + static_cast<uint32_t>(role) * 0x165667B1u);
			noise.Octaves = octaves;
			noise.Frequency = frequency;
			return noise;
		}

		float Noise(const FractalNoise3D& noise, const glm::vec2& point)
		{
			return SampleFractalNoise3D(noise, glm::vec3(point.x, 0.0f, point.y));
		}

		float SmoothStep(float from, float to, float x)
		{
			const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// A pin's pull at a point: its strength at its middle, falling smoothly to 0 at its radius, which
		// the noise (about -1 to 1) moves in and out by a third
		float PinPull(const SdfTerrainPlanPin& pin, const glm::vec2& point, float noise)
		{
			const float radius = std::max(pin.Radius, 1.0f) * (1.0f + 0.35f * std::clamp(noise, -1.0f, 1.0f));
			const glm::vec2 apart = point - pin.Position;
			const float t = std::min((apart.x * apart.x + apart.y * apart.y) / (radius * radius), 1.0f);
			return pin.Strength * (1.0f - t) * (1.0f - t);
		}

		float PinsPull(const SdfTerrainWorldPlan& made, ESdfTerrainLandform landform, const glm::vec2& point, float noise)
		{
			float pull = 0.0f;
			for (const SdfTerrainPlanPin& pin : made.Pins)
			{
				if (pin.Landform == landform)
				{
					pull += PinPull(pin, point, noise);
				}
			}
			return pull;
		}

		// The value with `share` of the values at or above it; above them all for none
		float Threshold(std::vector<float> values, float share)
		{
			if (values.empty() || !(share > 0.0f))
			{
				return std::numeric_limits<float>::max();
			}
			const size_t below = std::min(values.size() - 1, size_t(float(values.size()) * (1.0f - std::min(share, 1.0f))));
			std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(below), values.end());
			return values[below];
		}

		// Box blurs along each axis in turn, `passes` times, the grid's edge nodes repeated past it
		void Blur(std::vector<float>& grid, int nodes, int radius, int passes)
		{
			if (radius < 1)
			{
				return;
			}
			// Running sums of a row with its ends repeated `radius` nodes out
			std::vector<float> sums(size_t(nodes) + size_t(radius) * 2 + 1);
			const float scale = 1.0f / float(radius * 2 + 1);
			for (int pass = 0; pass < passes; ++pass)
			{
				for (int axis = 0; axis < 2; ++axis)
				{
					for (int row = 0; row < nodes; ++row)
					{
						auto at = [&](int n) -> float& { return axis == 0 ? grid[size_t(n) + size_t(row) * size_t(nodes)] : grid[size_t(row) + size_t(n) * size_t(nodes)]; };
						sums[0] = 0.0f;
						for (int n = -radius; n < nodes + radius; ++n)
						{
							sums[size_t(n + radius + 1)] = sums[size_t(n + radius)] + at(std::clamp(n, 0, nodes - 1));
						}
						for (int n = 0; n < nodes; ++n)
						{
							at(n) = (sums[size_t(n + radius * 2 + 1)] - sums[size_t(n)]) * scale;
						}
					}
				}
			}
		}

		// A blurred weight bent so that its share of the counted nodes is at 0.5 or above, as before the
		// blur: the weight its share has goes to 0.5, 0 and 1 stay
		void KeepShare(std::vector<float>& weight, const std::vector<uint8_t>& counted, float share)
		{
			if (!(share > 0.0f))
			{
				return;
			}
			std::vector<float> values;
			for (size_t index = 0; index < weight.size(); ++index)
			{
				if (counted[index] != 0)
				{
					values.push_back(weight[index]);
				}
			}
			const float middle = std::clamp(Threshold(std::move(values), share), 1e-3f, 1.0f - 1e-3f);
			for (float& value : weight)
			{
				value = value <= middle ? 0.5f * value / middle : 0.5f + 0.5f * (value - middle) / (1.0f - middle);
			}
		}

		// The distance from a point to a segment, and how far along it the nearest point is, 0 to 1
		float SegmentDistance(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b, float& outAlong)
		{
			const glm::vec2 along = b - a;
			const float length2 = along.x * along.x + along.y * along.y;
			outAlong = length2 > 0.0f ? std::clamp(((point.x - a.x) * along.x + (point.y - a.y) * along.y) / length2, 0.0f, 1.0f) : 0.0f;
			const glm::vec2 apart = point - (a + along * outAlong);
			return std::sqrt(apart.x * apart.x + apart.y * apart.y);
		}

		glm::vec2 Normalized(const glm::vec2& v)
		{
			const float length = std::sqrt(v.x * v.x + v.y * v.y);
			return length > 0.0f ? v / length : glm::vec2(1.0f, 0.0f);
		}

		// The plan's line of nodes cut into a grid as a distance (less a half width that changes along
		// it), where it is nearer than what is there; `outValue` takes `values` blended along the nearest
		// segment where it does
		void CutLine(TerrainWorldPlan& plan, const std::vector<glm::vec2>& points, const std::vector<float>& halfWidths, const std::vector<float>* values,
			std::vector<float>& grid, std::vector<float>* outValue)
		{
			for (size_t n = 0; n + 1 < points.size(); ++n)
			{
				const glm::vec2 a = points[n];
				const glm::vec2 b = points[n + 1];
				const float reach = std::max(halfWidths[n], halfWidths[n + 1]) + TerrainWorldPlan::MaxReach;
				const glm::ivec2 low = plan.NodeOf(glm::min(a, b) - reach);
				const glm::ivec2 high = plan.NodeOf(glm::max(a, b) + reach);
				for (int j = low.y; j <= high.y; ++j)
				{
					for (int i = low.x; i <= high.x; ++i)
					{
						float along = 0.0f;
						const float distance = SegmentDistance(plan.PointOf(i, j), a, b, along) - (halfWidths[n] + (halfWidths[n + 1] - halfWidths[n]) * along);
						const size_t index = plan.IndexOf(i, j);
						if (distance < grid[index])
						{
							grid[index] = distance;
							if (values != nullptr && outValue != nullptr)
							{
								(*outValue)[index] = (*values)[n] + ((*values)[n + 1] - (*values)[n]) * along;
							}
						}
					}
				}
			}
		}

		// Corners cut off a line twice (Chaikin's), its ends kept, its values blended the same
		void SmoothLine(std::vector<glm::vec2>& points, std::vector<float>& values)
		{
			for (int pass = 0; pass < 2 && points.size() > 2; ++pass)
			{
				std::vector<glm::vec2> smoothPoints{ points.front() };
				std::vector<float> smoothValues{ values.front() };
				for (size_t n = 0; n + 1 < points.size(); ++n)
				{
					smoothPoints.push_back(points[n] * 0.75f + points[n + 1] * 0.25f);
					smoothPoints.push_back(points[n] * 0.25f + points[n + 1] * 0.75f);
					smoothValues.push_back(values[n] * 0.75f + values[n + 1] * 0.25f);
					smoothValues.push_back(values[n] * 0.25f + values[n + 1] * 0.75f);
				}
				smoothPoints.push_back(points.back());
				smoothValues.push_back(values.back());
				points = std::move(smoothPoints);
				values = std::move(smoothValues);
			}
		}

		// How fast a grid blended between its nodes changes a metre at most
		float GridSlope(const std::vector<float>& grid, int nodes)
		{
			float alongX = 0.0f;
			float alongZ = 0.0f;
			for (int j = 0; j < nodes; ++j)
			{
				for (int i = 0; i < nodes; ++i)
				{
					const float value = grid[size_t(i) + size_t(j) * size_t(nodes)];
					if (i + 1 < nodes)
					{
						alongX = std::max(alongX, std::fabs(grid[size_t(i + 1) + size_t(j) * size_t(nodes)] - value));
					}
					if (j + 1 < nodes)
					{
						alongZ = std::max(alongZ, std::fabs(grid[size_t(i) + size_t(j + 1) * size_t(nodes)] - value));
					}
				}
			}
			// A little over, for the rounding of the blend
			return std::sqrt(alongX * alongX + alongZ * alongZ) / Cell * 1.001f + 1e-6f;
		}

		// A rift's line from outside the edge inland: towards its pin, if it has one, then on, bending
		// with the noise, never turning back towards the edge
		std::vector<glm::vec2> PlanRift(const TerrainWorldPlan& plan, const SdfTerrainWorldPlan& made, uint32_t seed, uint32_t index, const SdfTerrainPlanPin* pin)
		{
			glm::vec2 outward;
			if (pin != nullptr && pin->Position.x * pin->Position.x + pin->Position.y * pin->Position.y > 1.0f)
			{
				outward = Normalized(pin->Position);
			}
			else
			{
				glm::vec2 way(0.0f);
				for (uint32_t attempt = 0; attempt < 64; ++attempt)
				{
					way = glm::vec2(Random(seed, 10 + index, attempt * 2) * 2.0f - 1.0f, Random(seed, 10 + index, attempt * 2 + 1) * 2.0f - 1.0f);
					const float length2 = way.x * way.x + way.y * way.y;
					if (length2 > 0.04f && length2 <= 1.0f)
					{
						break;
					}
				}
				outward = Normalized(way);
			}
			// Out to the edge, then a rift's width past it, so the rift opens the edge
			float reach = 0.0f;
			const float farthest = made.Size;
			for (float r = 0.0f; r < farthest; r += Cell * 0.5f)
			{
				if (plan.EdgeValue(outward.x * r, outward.y * r) < 0.0f)
				{
					reach = r;
				}
			}
			glm::vec2 point = outward * (reach + made.RiftWidth);
			const glm::vec2 inward = -outward;
			glm::vec2 heading = inward;
			float length = std::max(made.RiftLength, 0.0f) + made.RiftWidth;
			bool bPassed = pin == nullptr;
			if (pin != nullptr)
			{
				length = std::max(length, glm::length(pin->Position - point) + made.RiftWidth * 2.0f);
			}
			const FractalNoise3D bendX = PlanNoise(seed + index, EPlanNoise::RiftBendX, 1.0f / std::max(made.RegionSize * 0.7f, 32.0f), 2);
			const FractalNoise3D bendZ = PlanNoise(seed + index, EPlanNoise::RiftBendZ, 1.0f / std::max(made.RegionSize * 0.7f, 32.0f), 2);
			std::vector<glm::vec2> points{ point };
			for (float travelled = 0.0f; travelled < length; travelled += Cell)
			{
				if (!bPassed)
				{
					heading = Normalized(heading * 0.6f + Normalized(pin->Position - point) * 0.4f);
				}
				heading = Normalized(heading + glm::vec2(Noise(bendX, point), Noise(bendZ, point)) * 0.35f);
				if (heading.x * inward.x + heading.y * inward.y < 0.2f)
				{
					heading = Normalized(heading + inward * 0.5f);
				}
				point += heading * Cell;
				points.push_back(point);
				if (!bPassed && glm::length(pin->Position - point) < Cell)
				{
					bPassed = true;
				}
			}
			return points;
		}

		void MakeBounds(TerrainWorldPlan& plan)
		{
			const int nodes = plan.Nodes;
			plan.Blocks = std::max(1, (nodes - 1 + TerrainWorldPlan::BlockNodes - 1) / TerrainWorldPlan::BlockNodes);
			plan.BlockBounds.assign(size_t(plan.Blocks) * size_t(plan.Blocks), {});
			for (int bj = 0; bj < plan.Blocks; ++bj)
			{
				for (int bi = 0; bi < plan.Blocks; ++bi)
				{
					TerrainWorldPlan::Bounds bounds;
					bounds.LowHeight = bounds.LowRift = bounds.LowChannel = bounds.LowBed = std::numeric_limits<float>::max();
					bounds.HighHeight = bounds.HighRift = bounds.HighChannel = bounds.HighBed = -std::numeric_limits<float>::max();
					for (int j = bj * TerrainWorldPlan::BlockNodes; j <= std::min((bj + 1) * TerrainWorldPlan::BlockNodes, nodes - 1); ++j)
					{
						for (int i = bi * TerrainWorldPlan::BlockNodes; i <= std::min((bi + 1) * TerrainWorldPlan::BlockNodes, nodes - 1); ++i)
						{
							const size_t index = plan.IndexOf(i, j);
							bounds.LowHeight = std::min(bounds.LowHeight, plan.Height[index]);
							bounds.HighHeight = std::max(bounds.HighHeight, plan.Height[index]);
							bounds.HighPlains = std::max(bounds.HighPlains, plan.PlainsAmplitude[index]);
							bounds.HighRough = std::max(bounds.HighRough, plan.RoughAmplitude[index]);
							bounds.LowRift = std::min(bounds.LowRift, plan.Rift[index]);
							bounds.HighRift = std::max(bounds.HighRift, plan.Rift[index]);
							bounds.LowChannel = std::min(bounds.LowChannel, plan.Channel[index]);
							bounds.HighChannel = std::max(bounds.HighChannel, plan.Channel[index]);
							bounds.LowBed = std::min(bounds.LowBed, plan.ChannelBed[index]);
							bounds.HighBed = std::max(bounds.HighBed, plan.ChannelBed[index]);
							bounds.HighTunnels = std::max(bounds.HighTunnels, plan.Tunnels[index]);
						}
					}
					plan.BlockBounds[size_t(bi) + size_t(bj) * size_t(plan.Blocks)] = bounds;
				}
			}
		}
	}

	float TerrainWorldPlan::At(const std::vector<float>& grid, float x, float z) const
	{
		const float last = float(Nodes - 1);
		const float fx = std::clamp((x - Origin.x) / CellSize, 0.0f, last);
		const float fz = std::clamp((z - Origin.y) / CellSize, 0.0f, last);
		const int i = std::min(int(fx), Nodes - 2);
		const int j = std::min(int(fz), Nodes - 2);
		const float tx = fx - float(i);
		const float tz = fz - float(j);
		const size_t index = IndexOf(i, j);
		const float a = grid[index] + (grid[index + 1] - grid[index]) * tx;
		const float b = grid[index + size_t(Nodes)] + (grid[index + size_t(Nodes) + 1] - grid[index + size_t(Nodes)]) * tx;
		return a + (b - a) * tz;
	}

	TerrainWorldPlan::Bounds TerrainWorldPlan::GetRange(const glm::vec2& boxMin, const glm::vec2& boxMax) const
	{
		const float last = float(Nodes - 1);
		const int i0 = int(std::clamp((boxMin.x - Origin.x) / CellSize, 0.0f, last));
		const int i1 = int(std::ceil(std::clamp((boxMax.x - Origin.x) / CellSize, 0.0f, last)));
		const int j0 = int(std::clamp((boxMin.y - Origin.y) / CellSize, 0.0f, last));
		const int j1 = int(std::ceil(std::clamp((boxMax.y - Origin.y) / CellSize, 0.0f, last)));
		// Block b holds nodes 8b to 8b + 8
		const int bi0 = std::clamp(i0 / BlockNodes, 0, Blocks - 1);
		const int bj0 = std::clamp(j0 / BlockNodes, 0, Blocks - 1);
		const int bi1 = std::clamp(std::max(i1 - 1, 0) / BlockNodes, bi0, Blocks - 1);
		const int bj1 = std::clamp(std::max(j1 - 1, 0) / BlockNodes, bj0, Blocks - 1);
		Bounds range = BlockBounds[size_t(bi0) + size_t(bj0) * size_t(Blocks)];
		for (int bj = bj0; bj <= bj1; ++bj)
		{
			for (int bi = bi0; bi <= bi1; ++bi)
			{
				const Bounds& block = BlockBounds[size_t(bi) + size_t(bj) * size_t(Blocks)];
				range.LowHeight = std::min(range.LowHeight, block.LowHeight);
				range.HighHeight = std::max(range.HighHeight, block.HighHeight);
				range.HighPlains = std::max(range.HighPlains, block.HighPlains);
				range.HighRough = std::max(range.HighRough, block.HighRough);
				range.LowRift = std::min(range.LowRift, block.LowRift);
				range.HighRift = std::max(range.HighRift, block.HighRift);
				range.LowChannel = std::min(range.LowChannel, block.LowChannel);
				range.HighChannel = std::max(range.HighChannel, block.HighChannel);
				range.LowBed = std::min(range.LowBed, block.LowBed);
				range.HighBed = std::max(range.HighBed, block.HighBed);
				range.HighTunnels = std::max(range.HighTunnels, block.HighTunnels);
			}
		}
		return range;
	}

	float TerrainWorldPlan::EdgeValue(float x, float z) const
	{
		return std::sqrt(x * x + z * z) - (Radius + Made.EdgeRoughness * SampleFractalNoise3D(EdgeNoise, glm::vec3(x, 0.0f, z)));
	}

	glm::ivec2 TerrainWorldPlan::NodeOf(const glm::vec2& point) const
	{
		const glm::vec2 at = (point - Origin) / CellSize;
		return glm::ivec2(std::clamp(int(std::floor(at.x + 0.5f)), 0, Nodes - 1), std::clamp(int(std::floor(at.y + 0.5f)), 0, Nodes - 1));
	}

	std::shared_ptr<const TerrainWorldPlan> MakeTerrainWorldPlan(uint32_t seed, const SdfTerrainWorldPlan& made)
	{
		auto plan = std::make_shared<TerrainWorldPlan>();
		plan->Seed = seed;
		plan->Made = made;
		const float size = std::max(made.Size, Cell * 8.0f);
		const float regionSize = std::max(made.RegionSize, Cell * 2.0f);
		const int nodes = int(std::ceil(size / Cell)) + 5;
		plan->Nodes = nodes;
		plan->Origin = glm::vec2(-float(nodes - 1) * Cell * 0.5f);
		plan->Radius = std::max(size * 0.5f - std::fabs(made.EdgeRoughness) * 0.7f - Cell * 2.0f, size * 0.1f);
		plan->EdgeNoise = PlanNoise(seed, EPlanNoise::Edge, 1.0f / (regionSize * 2.0f), 4);
		const size_t count = size_t(nodes) * size_t(nodes);
		auto forEach = [&](auto&& visit)
		{
			for (int j = 0; j < nodes; ++j)
			{
				for (int i = 0; i < nodes; ++i)
				{
					visit(i, j, plan->IndexOf(i, j), plan->PointOf(i, j));
				}
			}
		};

		// The landmass, and how far inland each node is, 0 at the edge to 1 halfway to the middle
		std::vector<uint8_t> land(count);
		std::vector<float> inland(count);
		forEach([&](int, int, size_t index, const glm::vec2& point)
			{
				const float edge = plan->EdgeValue(point.x, point.y);
				land[index] = edge < 0.0f ? 1 : 0;
				inland[index] = std::clamp(-edge / (plan->Radius * 0.5f), 0.0f, 1.0f);
			});

		// How far each pin's edge goes in and out there
		const FractalNoise3D pinNoise = PlanNoise(seed, EPlanNoise::Pins, 1.0f / (regionSize * 0.5f), 3);
		std::vector<float> pinEdge(count);
		forEach([&](int, int, size_t index, const glm::vec2& point) { pinEdge[index] = Noise(pinNoise, point) * 1.5f; });
		auto pull = [&](ESdfTerrainLandform landform, size_t index, const glm::vec2& point) { return PinsPull(made, landform, point, pinEdge[index]); };

		// Sea: where its noise, leaning towards the edge, and its pins are highest, for its share of the
		// land
		const FractalNoise3D seaNoise = PlanNoise(seed, EPlanNoise::Sea, 1.0f / regionSize, 3);
		std::vector<float> seaScore(count);
		std::vector<float> scores;
		forEach([&](int, int, size_t index, const glm::vec2& point)
			{
				seaScore[index] = Noise(seaNoise, point) + 0.7f * (1.0f - inland[index]) + 2.0f * pull(ESdfTerrainLandform::Sea, index, point)
					- 2.0f * (pull(ESdfTerrainLandform::Plains, index, point) + pull(ESdfTerrainLandform::Roughland, index, point));
				if (land[index] != 0)
				{
					scores.push_back(seaScore[index]);
				}
			});
		const float seaThreshold = Threshold(scores, std::clamp(made.Sea, 0.0f, 0.8f));
		std::vector<float> sea(count);
		forEach([&](int, int, size_t index, const glm::vec2&) { sea[index] = land[index] != 0 && seaScore[index] >= seaThreshold ? 1.0f : 0.0f; });

		// Rough land: the same for its share of the land above the sea
		const FractalNoise3D roughNoise = PlanNoise(seed, EPlanNoise::Rough, 1.0f / regionSize, 3);
		std::vector<float> roughScore(count);
		scores.clear();
		forEach([&](int, int, size_t index, const glm::vec2& point)
			{
				roughScore[index] = Noise(roughNoise, point) + 2.0f * (pull(ESdfTerrainLandform::Roughland, index, point) - pull(ESdfTerrainLandform::Plains, index, point));
				if (land[index] != 0 && sea[index] == 0.0f)
				{
					scores.push_back(roughScore[index]);
				}
			});
		const float roughThreshold = Threshold(scores, std::clamp(made.Roughland, 0.0f, 1.0f));
		std::vector<float> rough(count);
		forEach([&](int, int, size_t index, const glm::vec2&) { rough[index] = land[index] != 0 && sea[index] == 0.0f && roughScore[index] >= roughThreshold ? 1.0f : 0.0f; });

		// Each blurred into a weight that goes from 0 to 1 over a few nodes, so they blend; the sea over
		// more, so its shores slope
		const int blur = std::max(1, int(regionSize * 0.08f / Cell));
		Blur(sea, nodes, std::max(1, int(regionSize * 0.2f / Cell)), 2);
		Blur(rough, nodes, blur, 2);
		KeepShare(sea, land, std::clamp(made.Sea, 0.0f, 0.8f));
		std::vector<uint8_t> aboveSea(count);
		forEach([&](int, int, size_t index, const glm::vec2&) { aboveSea[index] = land[index] != 0 && sea[index] < 0.5f ? 1 : 0; });
		KeepShare(rough, aboveSea, std::clamp(made.Roughland, 0.0f, 1.0f));

		// Tunnels: the same for their share of the land above the sea, leaning towards rough land
		const FractalNoise3D tunnelNoise = PlanNoise(seed, EPlanNoise::Tunnels, 1.0f / regionSize, 3);
		std::vector<float> tunnelScore(count);
		scores.clear();
		forEach([&](int, int, size_t index, const glm::vec2& point)
			{
				tunnelScore[index] = Noise(tunnelNoise, point) + 0.8f * rough[index];
				if (land[index] != 0 && sea[index] < 0.5f)
				{
					scores.push_back(tunnelScore[index]);
				}
			});
		const float tunnelThreshold = Threshold(scores, std::clamp(made.Tunnels, 0.0f, 1.0f));
		std::vector<float> tunnels(count);
		forEach([&](int, int, size_t index, const glm::vec2&) { tunnels[index] = land[index] != 0 && sea[index] < 0.5f && tunnelScore[index] >= tunnelThreshold ? 1.0f : 0.0f; });
		Blur(tunnels, nodes, blur, 2);
		KeepShare(tunnels, aboveSea, std::clamp(made.Tunnels, 0.0f, 1.0f));

		// The base heights
		std::vector<float>& height = plan->Height;
		height.resize(count);
		forEach([&](int, int, size_t index, const glm::vec2&)
			{
				const float ground = made.Height + rough[index] * made.RoughLift;
				height[index] = ground + ((made.SeaLevel - made.SeaDepth) - ground) * sea[index];
			});

		// Rifts, those pins ask for first, from outside the edge inland
		plan->Rift.assign(count, TerrainWorldPlan::MaxReach);
		std::vector<std::vector<glm::vec2>> rifts;
		for (const SdfTerrainPlanPin& pin : made.Pins)
		{
			if (pin.Landform == ESdfTerrainLandform::Rift)
			{
				rifts.push_back(PlanRift(*plan, made, seed, uint32_t(rifts.size()), &pin));
			}
		}
		for (uint32_t n = 0; n < made.Rifts; ++n)
		{
			rifts.push_back(PlanRift(*plan, made, seed, uint32_t(rifts.size()), nullptr));
		}
		for (const std::vector<glm::vec2>& rift : rifts)
		{
			// Narrowing from full width at the edge to nothing at its inland end
			std::vector<float> halfWidths(rift.size());
			for (size_t n = 0; n < rift.size(); ++n)
			{
				const float left = 1.0f - float(n) / float(std::max<size_t>(rift.size() - 1, 1));
				halfWidths[n] = std::max(made.RiftWidth, 0.0f) * 0.5f * std::sqrt(left);
			}
			CutLine(*plan, rift, halfWidths, nullptr, plan->Rift, nullptr);
		}
		for (float& value : plan->Rift)
		{
			value = std::min(value, TerrainWorldPlan::MaxReach);
		}

		// Lakes dug into the plains, away from the sea, rough land, rifts and the edge, a basin's width
		// and more apart
		std::vector<float> basin(count, 0.0f);
		{
			std::vector<std::pair<uint32_t, size_t>> candidates;
			forEach([&](int, int, size_t index, const glm::vec2&)
				{
					if (land[index] != 0 && sea[index] < 0.05f && rough[index] < 0.1f && inland[index] > 0.35f
						&& plan->Rift[index] > std::min(made.LakeRadius, TerrainWorldPlan::MaxReach * 0.9f))
					{
						candidates.emplace_back(Mix(seed ^ Mix(uint32_t(index))), index);
					}
				});
			std::sort(candidates.begin(), candidates.end());
			std::vector<glm::vec2> centers;
			for (const auto& [order, index] : candidates)
			{
				if (centers.size() >= made.Lakes)
				{
					break;
				}
				const glm::vec2 point = plan->PointOf(int(index % size_t(nodes)), int(index / size_t(nodes)));
				const bool bApart = std::all_of(centers.begin(), centers.end(), [&](const glm::vec2& other) { return glm::length(other - point) > made.LakeRadius * 4.0f; });
				if (bApart)
				{
					centers.push_back(point);
				}
			}
			const FractalNoise3D shoreNoise = PlanNoise(seed, EPlanNoise::Lakes, 1.0f / std::max(made.LakeRadius, Cell), 2);
			for (const glm::vec2& center : centers)
			{
				const float farthest = std::max(made.LakeRadius, Cell) * 1.3f * 1.3f;
				const glm::ivec2 low = plan->NodeOf(center - farthest);
				const glm::ivec2 high = plan->NodeOf(center + farthest);
				for (int j = low.y; j <= high.y; ++j)
				{
					for (int i = low.x; i <= high.x; ++i)
					{
						const size_t index = plan->IndexOf(i, j);
						const glm::vec2 point = plan->PointOf(i, j);
						const float reach = std::max(made.LakeRadius, Cell) * 1.3f * (1.0f + 0.3f * std::clamp(Noise(shoreNoise, point), -1.0f, 1.0f));
						const glm::vec2 apart = point - center;
						const float t = std::min((apart.x * apart.x + apart.y * apart.y) / (reach * reach), 1.0f);
						const float bowl = (1.0f - t) * (1.0f - t);
						basin[index] = std::max(basin[index], bowl);
						height[index] -= made.LakeDepth * bowl;
					}
				}
				plan->Lakes.push_back(TerrainWorldPlan::Lake{ center, 0.0f });
			}
		}

		// Where water runs: each node's heights filled up to where it spills (priority flood from the
		// sea, the edge and the rifts), each node draining to the one it was reached from
		// Over heights made uneven by a few metres, away from the lakes, so the rivers wind
		const auto isOutlet = [&](size_t index) { return land[index] == 0 || sea[index] >= 0.5f || plan->Rift[index] < 0.0f; };
		const FractalNoise3D meanderNoise = PlanNoise(seed, EPlanNoise::Meander, 1.0f / 160.0f, 3);
		std::vector<float> uneven(count);
		forEach([&](int, int, size_t index, const glm::vec2& point) { uneven[index] = height[index] + 3.0f * Noise(meanderNoise, point) * (1.0f - basin[index]); });
		std::vector<float> filled(count, std::numeric_limits<float>::max());
		std::vector<int32_t> drain(count, -1);
		{
			using Entry = std::pair<float, uint32_t>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
			for (size_t index = 0; index < count; ++index)
			{
				if (isOutlet(index))
				{
					filled[index] = uneven[index];
					open.emplace(filled[index], uint32_t(index));
				}
			}
			std::vector<uint8_t> done(count, 0);
			while (!open.empty())
			{
				const auto [level, at] = open.top();
				open.pop();
				if (done[at] != 0)
				{
					continue;
				}
				done[at] = 1;
				const int i = int(at % uint32_t(nodes));
				const int j = int(at / uint32_t(nodes));
				for (int dj = -1; dj <= 1; ++dj)
				{
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = i + di;
						const int nj = j + dj;
						if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= nodes || nj >= nodes)
						{
							continue;
						}
						const size_t next = plan->IndexOf(ni, nj);
						const float nextLevel = std::max(uneven[next], level + 0.001f);
						if (done[next] == 0 && nextLevel < filled[next])
						{
							filled[next] = nextLevel;
							drain[next] = int32_t(at);
							open.emplace(nextLevel, uint32_t(next));
						}
					}
				}
			}
		}
		// A basin dug too near an outlet drains into it: a hollow, not a lake
		for (TerrainWorldPlan::Lake& lake : plan->Lakes)
		{
			const glm::ivec2 node = plan->NodeOf(lake.Center);
			lake.Level = filled[plan->IndexOf(node.x, node.y)];
		}
		std::erase_if(plan->Lakes, [&](const TerrainWorldPlan::Lake& lake) { return !(lake.Level > plan->At(height, lake.Center.x, lake.Center.y) + 1.0f); });

		// Rivers from the highest ground, sources apart, each down its drains to an outlet or another
		// river
		plan->Channel.assign(count, TerrainWorldPlan::MaxReach);
		plan->ChannelBed.resize(count);
		for (size_t index = 0; index < count; ++index)
		{
			plan->ChannelBed[index] = height[index] - made.RiverDepth;
		}
		{
			std::vector<std::pair<float, size_t>> sources;
			forEach([&](int, int, size_t index, const glm::vec2&)
				{
					if (!isOutlet(index) && inland[index] > 0.3f && basin[index] < 0.05f)
					{
						sources.emplace_back(-(height[index] + rough[index] * 1000.0f), index);
					}
				});
			std::sort(sources.begin(), sources.end());
			std::vector<int32_t> riverOf(count, -1);
			std::vector<glm::vec2> starts;
			const float spacing = std::max(regionSize, 250.0f);
			for (const auto& [order, source] : sources)
			{
				if (plan->Rivers.size() >= made.Rivers)
				{
					break;
				}
				const glm::vec2 start = plan->PointOf(int(source % size_t(nodes)), int(source / size_t(nodes)));
				if (!std::all_of(starts.begin(), starts.end(), [&](const glm::vec2& other) { return glm::length(other - start) > spacing; }) || riverOf[source] >= 0)
				{
					continue;
				}
				std::vector<size_t> course{ source };
				while (drain[course.back()] >= 0 && riverOf[course.back()] < 0 && !isOutlet(course.back()))
				{
					course.push_back(size_t(drain[course.back()]));
				}
				if (course.size() < 8)
				{
					continue;
				}
				TerrainWorldPlan::River river;
				const size_t last = course.back();
				river.End = riverOf[last] >= 0 ? TerrainWorldPlan::River::EEnd::River
					: land[last] == 0         ? TerrainWorldPlan::River::EEnd::Edge
					: plan->Rift[last] < 0.0f ? TerrainWorldPlan::River::EEnd::Rift
											  : TerrainWorldPlan::River::EEnd::Sea;
				float bed = std::numeric_limits<float>::max();
				for (const size_t index : course)
				{
					if (riverOf[index] < 0)
					{
						riverOf[index] = int32_t(plan->Rivers.size());
					}
					bed = std::min(bed, filled[index] - made.RiverDepth);
					river.Points.push_back(plan->PointOf(int(index % size_t(nodes)), int(index / size_t(nodes))));
					river.Beds.push_back(bed);
				}
				SmoothLine(river.Points, river.Beds);
				for (size_t n = 1; n < river.Beds.size(); ++n)
				{
					river.Beds[n] = std::min(river.Beds[n], river.Beds[n - 1]);
				}
				std::vector<float> halfWidths(river.Points.size());
				for (size_t n = 0; n < halfWidths.size(); ++n)
				{
					halfWidths[n] = std::max(made.RiverWidth, 0.0f) * 0.5f * (1.0f + 0.5f * float(n) / float(halfWidths.size()));
				}
				CutLine(*plan, river.Points, halfWidths, &river.Beds, plan->Channel, &plan->ChannelBed);
				starts.push_back(start);
				plan->Rivers.push_back(std::move(river));
			}
			for (float& value : plan->Channel)
			{
				value = std::min(value, TerrainWorldPlan::MaxReach);
			}
		}

		// How far the top's noise lifts and lowers it: the plains' and rough land's by their weights,
		// calmer under the sea, in lakes and towards rivers
		plan->PlainsAmplitude.resize(count);
		plan->RoughAmplitude.resize(count);
		plan->Tunnels.resize(count);
		plan->Landform.resize(count);
		forEach([&](int, int, size_t index, const glm::vec2& point)
			{
				const float calm = SmoothStep(0.0f, 80.0f, plan->Channel[index]) * (1.0f - basin[index]);
				plan->PlainsAmplitude[index] = std::max(made.PlainsHeight, 0.0f) * (1.0f - rough[index]) * (1.0f - 0.6f * sea[index]) * calm;
				plan->RoughAmplitude[index] = std::max(made.RoughHeight, 0.0f) * rough[index] * (1.0f - sea[index]) * calm;
				plan->Tunnels[index] = std::clamp((tunnels[index] + pull(ESdfTerrainLandform::Tunnels, index, point)) * (1.0f - sea[index]), 0.0f, 1.0f);
				const ESdfTerrainLandform landform = sea[index] >= 0.5f ? ESdfTerrainLandform::Sea
					: plan->Rift[index] < 0.0f                          ? ESdfTerrainLandform::Rift
					: rough[index] >= 0.5f                              ? ESdfTerrainLandform::Roughland
																		: ESdfTerrainLandform::Plains;
				plan->Landform[index] = land[index] != 0 ? uint8_t(landform) : TerrainWorldPlan::NotLand;
			});

		MakeBounds(*plan);
		plan->HeightSlope = GridSlope(plan->Height, nodes);
		plan->PlainsSlope = GridSlope(plan->PlainsAmplitude, nodes);
		plan->RoughSlope = GridSlope(plan->RoughAmplitude, nodes);
		plan->RiftSlope = GridSlope(plan->Rift, nodes);
		plan->ChannelSlope = GridSlope(plan->Channel, nodes);
		plan->BedSlope = GridSlope(plan->ChannelBed, nodes);
		plan->TunnelsSlope = GridSlope(plan->Tunnels, nodes);
		plan->MaxPlainsAmplitude = *std::max_element(plan->PlainsAmplitude.begin(), plan->PlainsAmplitude.end());
		plan->MaxRoughAmplitude = *std::max_element(plan->RoughAmplitude.begin(), plan->RoughAmplitude.end());
		plan->bTunnels = std::any_of(plan->Tunnels.begin(), plan->Tunnels.end(), [](float tunnels) { return tunnels > 0.0f; });
		return plan;
	}

	bool IsSameTerrainWorldPlan(const SdfTerrainWorldPlan& a, const SdfTerrainWorldPlan& b)
	{
		const bool bSamePins = a.Pins.size() == b.Pins.size()
			&& std::equal(a.Pins.begin(), a.Pins.end(), b.Pins.begin(),
				[](const SdfTerrainPlanPin& x, const SdfTerrainPlanPin& y) { return x.Landform == y.Landform && x.Position == y.Position && x.Radius == y.Radius && x.Strength == y.Strength; });
		return bSamePins && a.Size == b.Size && a.RegionSize == b.RegionSize && a.Height == b.Height && a.Thickness == b.Thickness && a.UndersideDepth == b.UndersideDepth
			&& a.EdgeRoughness == b.EdgeRoughness && a.Rounding == b.Rounding && a.Sea == b.Sea && a.SeaLevel == b.SeaLevel && a.SeaDepth == b.SeaDepth
			&& a.Roughland == b.Roughland && a.RoughLift == b.RoughLift && a.RoughHeight == b.RoughHeight && a.PlainsHeight == b.PlainsHeight && a.Rifts == b.Rifts
			&& a.RiftLength == b.RiftLength && a.RiftWidth == b.RiftWidth && a.RiftDepth == b.RiftDepth && a.RiftFlare == b.RiftFlare && a.RiftRoughness == b.RiftRoughness
			&& a.Rivers == b.Rivers && a.RiverWidth == b.RiverWidth && a.RiverDepth == b.RiverDepth && a.Lakes == b.Lakes && a.LakeRadius == b.LakeRadius
			&& a.LakeDepth == b.LakeDepth && a.Tunnels == b.Tunnels && a.TunnelWidth == b.TunnelWidth && a.TunnelSpacing == b.TunnelSpacing;
	}

	uint64_t HashTerrainWorldPlan(const SdfTerrainWorldPlan& plan)
	{
		uint64_t hash = 0xCBF29CE484222325ull;
		auto add = [&](const auto& value)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			for (size_t i = 0; i < sizeof(value); ++i)
			{
				hash = (hash ^ bytes[i]) * 0x100000001B3ull;
			}
		};
		for (const float value : { plan.Size, plan.RegionSize, plan.Height, plan.Thickness, plan.UndersideDepth, plan.EdgeRoughness, plan.Rounding, plan.Sea, plan.SeaLevel,
				 plan.SeaDepth, plan.Roughland, plan.RoughLift, plan.RoughHeight, plan.PlainsHeight, plan.RiftLength, plan.RiftWidth, plan.RiftDepth, plan.RiftFlare,
				 plan.RiftRoughness, plan.RiverWidth, plan.RiverDepth, plan.LakeRadius, plan.LakeDepth, plan.Tunnels, plan.TunnelWidth, plan.TunnelSpacing })
		{
			add(value);
		}
		add(plan.Rifts);
		add(plan.Rivers);
		add(plan.Lakes);
		add(plan.Pins.size());
		for (const SdfTerrainPlanPin& pin : plan.Pins)
		{
			add(pin.Landform);
			add(pin.Position);
			add(pin.Radius);
			add(pin.Strength);
		}
		return hash;
	}

	std::shared_ptr<const TerrainWorldPlan> GetTerrainWorldPlan(const SdfTerrainComponent& terrain)
	{
		if (terrain.Planned && terrain.Planned->Seed == terrain.Seed && IsSameTerrainWorldPlan(terrain.Planned->Made, terrain.Plan))
		{
			return terrain.Planned;
		}
		// The last few made, for terrains whose own isn't up to date: chunks' copies, tests
		static std::mutex Lock;
		static std::vector<std::shared_ptr<const TerrainWorldPlan>> Kept;
		std::lock_guard<std::mutex> lock(Lock);
		for (const std::shared_ptr<const TerrainWorldPlan>& plan : Kept)
		{
			if (plan->Seed == terrain.Seed && IsSameTerrainWorldPlan(plan->Made, terrain.Plan))
			{
				return plan;
			}
		}
		std::shared_ptr<const TerrainWorldPlan> plan = MakeTerrainWorldPlan(terrain.Seed, terrain.Plan);
		Kept.push_back(plan);
		if (Kept.size() > 4)
		{
			Kept.erase(Kept.begin());
		}
		return plan;
	}

	bool PrepareTerrainWorldPlan(SdfTerrainComponent& terrain)
	{
		if (!UsesTerrainWorldPlan(terrain))
		{
			return false;
		}
		std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		if (plan == terrain.Planned)
		{
			return false;
		}
		terrain.Planned = std::move(plan);
		return true;
	}

	bool UsesTerrainWorldPlan(const SdfTerrainComponent& terrain)
	{
		return std::any_of(terrain.Layers.begin(), terrain.Layers.end(), [](const SdfTerrainLayer& layer) { return layer.bEnabled && layer.Kind == ESdfTerrainLayerKind::Plan; });
	}
}

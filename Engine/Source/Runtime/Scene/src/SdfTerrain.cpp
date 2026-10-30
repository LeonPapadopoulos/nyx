#include "NyxPCH.h"
#include "SdfTerrain.h"

#include "GradientNoise.h"
#include "SmoothMinimum.h"

#include <algorithm>
#include <array>
#include <cmath>

// Every operation as written, never fused into a multiply-add: the terrain is the same bits wherever
// it is sampled
#pragma fp_contract(off)

namespace Nyx
{
	namespace
	{
		// Points sampled together, a multiple of eight for the noise's batches
		constexpr size_t BlockSize = 64;

		// Which of a layer's noises: each its own seed
		enum class ENoiseRole : uint32_t
		{
			Main,
			Second,
			Third
		};

		FractalNoise3D MakeNoise(const SdfTerrainComponent& terrain, const SdfTerrainLayer& layer, ENoiseRole role)
		{
			FractalNoise3D noise;
			noise.Seed = terrain.Seed * 0x85EBCA77u + layer.Seed * 0xC2B2AE3Du + static_cast<uint32_t>(role) * 0x27D4EB2Fu;
			noise.Octaves = layer.Octaves;
			noise.Frequency = layer.Frequency;
			noise.Lacunarity = layer.Lacunarity;
			noise.Gain = layer.Gain;
			return noise;
		}

		void SampleNoise(const FractalNoise3D& noise, bool bRidged, const float* x, const float* y, const float* z, size_t count, float* out)
		{
			if (bRidged)
			{
				SampleRidgedNoise3DBatch(noise, x, y, z, count, out);
			}
			else
			{
				SampleFractalNoise3DBatch(noise, x, y, z, count, out);
			}
		}

		// What a noise can be: fractal within one octave's proven bound, ridged from 0 to 1
		float NoiseLowest(bool bRidged)
		{
			return bRidged ? 0.0f : -GradientNoise3DMaxValue;
		}

		float NoiseHighest(bool bRidged)
		{
			return bRidged ? 1.0f : GradientNoise3DMaxValue;
		}

		float NoiseSlope(const FractalNoise3D& noise, bool bRidged)
		{
			return bRidged ? GetRidgedNoise3DMaxSlope(noise) : GetFractalNoise3DMaxSlope(noise);
		}

		float Fade(float t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		// The fade's steepest, at t = 0.5
		constexpr float FadeMaxSlope = 1.875f;

		float GetTerraceSharpness(const SdfTerrainLayer& layer)
		{
			return std::clamp(layer.TerraceSharpness, 0.0f, 0.9f);
		}

		// A height made into steps of TerraceHeight: each step's lower part flat, its upper part a face
		// rising to the next, by the fade, so the steps stay smooth
		float Terrace(const SdfTerrainLayer& layer, float height)
		{
			if (!(layer.TerraceHeight > 0.0f))
			{
				return height;
			}
			const float sharpness = GetTerraceSharpness(layer);
			const float steps = height / layer.TerraceHeight;
			const float step = std::floor(steps);
			const float within = std::clamp((steps - step - sharpness * 0.5f) / (1.0f - sharpness), 0.0f, 1.0f);
			return (step + Fade(within)) * layer.TerraceHeight;
		}

		// How much steeper terracing makes a height, and how far it can move it
		float TerraceSlope(const SdfTerrainLayer& layer)
		{
			return layer.TerraceHeight > 0.0f ? FadeMaxSlope / (1.0f - GetTerraceSharpness(layer)) : 1.0f;
		}

		float TerraceReach(const SdfTerrainLayer& layer)
		{
			return layer.TerraceHeight > 0.0f ? layer.TerraceHeight : 0.0f;
		}

		// Noise in metres for caves: half a lattice cube a unit of noise
		float CaveScale(const SdfTerrainLayer& layer)
		{
			return 0.5f / layer.Frequency;
		}

		bool IsLayerActive(const SdfTerrainLayer& layer)
		{
			if (!layer.bEnabled)
			{
				return false;
			}
			const bool bCave = layer.Kind == ESdfTerrainLayerKind::Tunnels || layer.Kind == ESdfTerrainLayerKind::Chambers;
			return !bCave || layer.Frequency > 1e-6f;
		}

		struct Interval
		{
			float Low = 0.0f;
			float High = 0.0f;
		};

		Interval Scaled(float scale, float low, float high)
		{
			const float a = scale * low;
			const float b = scale * high;
			return { std::min(a, b), std::max(a, b) };
		}

		Interval MaxOf(const Interval& a, const Interval& b)
		{
			return { std::max(a.Low, b.Low), std::max(a.High, b.High) };
		}

		// The band of heights caves stay in: negative inside it
		float BandValue(const SdfTerrainLayer& layer, float y)
		{
			return std::max((layer.Height - layer.Depth) - y, y - layer.Height);
		}

		Interval BandRange(const SdfTerrainLayer& layer, float lowY, float highY)
		{
			const Interval below{ (layer.Height - layer.Depth) - highY, (layer.Height - layer.Depth) - lowY };
			const Interval above{ lowY - layer.Height, highY - layer.Height };
			return MaxOf(below, above);
		}

		// One block of points: what one layer's own shape is there (for Detail, how far it pushes)
		struct Block
		{
			const float* X = nullptr;
			const float* Y = nullptr;
			const float* Z = nullptr;
			size_t Count = 0;
		};

		void SampleLayer(const SdfTerrainComponent& terrain, const SdfTerrainLayer& layer, const Block& block, float* out)
		{
			static const std::array<float, BlockSize> Zeros{};
			std::array<float, BlockSize> first;
			std::array<float, BlockSize> second;
			std::array<float, BlockSize> third;

			switch (layer.Kind)
			{
			case ESdfTerrainLayerKind::Heightfield:
			{
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Main), layer.bRidged, block.X, Zeros.data(), block.Z, block.Count, first.data());
				for (size_t i = 0; i < block.Count; ++i)
				{
					const float height = Terrace(layer, layer.Height + layer.Amplitude * first[i]);
					out[i] = block.Y[i] - height;
				}
				break;
			}
			case ESdfTerrainLayerKind::Landmass:
			{
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Main), layer.bRidged, block.X, Zeros.data(), block.Z, block.Count, first.data());
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Second), true, block.X, Zeros.data(), block.Z, block.Count, second.data());
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Third), false, block.X, Zeros.data(), block.Z, block.Count, third.data());
				for (size_t i = 0; i < block.Count; ++i)
				{
					const float top = Terrace(layer, layer.Height + layer.Amplitude * first[i]);
					const float underside = (layer.Height - layer.Depth) - layer.UndersideDepth * second[i];
					const float slab = std::max(block.Y[i] - top, underside - block.Y[i]);
					const float dx = block.X[i] - layer.Center.x;
					const float dz = block.Z[i] - layer.Center.y;
					const float edge = std::sqrt(dx * dx + dz * dz) - (layer.Radius + layer.EdgeRoughness * third[i]);
					out[i] = SmoothMaximum(slab, edge, layer.Rounding);
				}
				break;
			}
			case ESdfTerrainLayerKind::Tunnels:
			{
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Main), false, block.X, block.Y, block.Z, block.Count, first.data());
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Second), false, block.X, block.Y, block.Z, block.Count, second.data());
				const float scale = CaveScale(layer);
				for (size_t i = 0; i < block.Count; ++i)
				{
					const float tunnel = scale * (std::fabs(first[i]) + std::fabs(second[i])) - layer.Radius * 0.5f;
					out[i] = std::max(tunnel, BandValue(layer, block.Y[i]));
				}
				break;
			}
			case ESdfTerrainLayerKind::Chambers:
			{
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Main), layer.bRidged, block.X, block.Y, block.Z, block.Count, first.data());
				const float scale = CaveScale(layer);
				const float threshold = 1.0f - layer.Openness;
				for (size_t i = 0; i < block.Count; ++i)
				{
					out[i] = std::max(scale * (threshold - first[i]), BandValue(layer, block.Y[i]));
				}
				break;
			}
			case ESdfTerrainLayerKind::Detail:
			{
				SampleNoise(MakeNoise(terrain, layer, ENoiseRole::Main), false, block.X, block.Y, block.Z, block.Count, first.data());
				for (size_t i = 0; i < block.Count; ++i)
				{
					out[i] = layer.Amplitude * first[i];
				}
				break;
			}
			}
		}

		// The layer's own slope bound
		float LayerSlope(const SdfTerrainComponent& terrain, const SdfTerrainLayer& layer)
		{
			const FractalNoise3D main = MakeNoise(terrain, layer, ENoiseRole::Main);
			switch (layer.Kind)
			{
			case ESdfTerrainLayerKind::Heightfield:
			{
				const float horizontal = std::fabs(layer.Amplitude) * NoiseSlope(main, layer.bRidged) * TerraceSlope(layer);
				return std::sqrt(1.0f + horizontal * horizontal);
			}
			case ESdfTerrainLayerKind::Landmass:
			{
				const float top = std::fabs(layer.Amplitude) * NoiseSlope(main, layer.bRidged) * TerraceSlope(layer);
				const float underside = std::fabs(layer.UndersideDepth) * NoiseSlope(MakeNoise(terrain, layer, ENoiseRole::Second), true);
				const float edge = 1.0f + std::fabs(layer.EdgeRoughness) * NoiseSlope(MakeNoise(terrain, layer, ENoiseRole::Third), false);
				return std::max({ std::sqrt(1.0f + top * top), std::sqrt(1.0f + underside * underside), edge });
			}
			case ESdfTerrainLayerKind::Tunnels:
				return std::max(CaveScale(layer) * (NoiseSlope(main, false) + NoiseSlope(MakeNoise(terrain, layer, ENoiseRole::Second), false)), 1.0f);
			case ESdfTerrainLayerKind::Chambers:
				return std::max(CaveScale(layer) * NoiseSlope(main, layer.bRidged), 1.0f);
			case ESdfTerrainLayerKind::Detail:
				return std::fabs(layer.Amplitude) * NoiseSlope(main, false);
			}
			return 0.0f;
		}

		// What the layer's own value can be in the box
		Interval LayerRange(const SdfTerrainLayer& layer, const glm::vec3& boxMin, const glm::vec3& boxMax)
		{
			switch (layer.Kind)
			{
			case ESdfTerrainLayerKind::Heightfield:
			{
				const Interval noise = Scaled(layer.Amplitude, NoiseLowest(layer.bRidged), NoiseHighest(layer.bRidged));
				const float lowest = layer.Height + noise.Low - TerraceReach(layer);
				const float highest = layer.Height + noise.High + TerraceReach(layer);
				return { boxMin.y - highest, boxMax.y - lowest };
			}
			case ESdfTerrainLayerKind::Landmass:
			{
				const Interval topNoise = Scaled(layer.Amplitude, NoiseLowest(layer.bRidged), NoiseHighest(layer.bRidged));
				const float topLowest = layer.Height + topNoise.Low - TerraceReach(layer);
				const float topHighest = layer.Height + topNoise.High + TerraceReach(layer);
				const Interval hang = Scaled(layer.UndersideDepth, 0.0f, 1.0f);
				const float undersideLowest = (layer.Height - layer.Depth) - hang.High;
				const float undersideHighest = (layer.Height - layer.Depth) - hang.Low;
				const Interval slab = MaxOf({ boxMin.y - topHighest, boxMax.y - topLowest }, { undersideLowest - boxMax.y, undersideHighest - boxMin.y });

				const glm::vec2 nearest = glm::clamp(layer.Center, glm::vec2(boxMin.x, boxMin.z), glm::vec2(boxMax.x, boxMax.z));
				const glm::vec2 farthest(std::max(std::fabs(boxMin.x - layer.Center.x), std::fabs(boxMax.x - layer.Center.x)),
					std::max(std::fabs(boxMin.z - layer.Center.y), std::fabs(boxMax.z - layer.Center.y)));
				const Interval rough = Scaled(layer.EdgeRoughness, -GradientNoise3DMaxValue, GradientNoise3DMaxValue);
				const Interval edge{ glm::length(nearest - layer.Center) - (layer.Radius + rough.High), glm::length(farthest) - (layer.Radius + rough.Low) };
				const Interval joined = MaxOf(slab, edge);
				return { joined.Low, joined.High + SmoothMinimumMaxOffset(layer.Rounding) };
			}
			case ESdfTerrainLayerKind::Tunnels:
			{
				const float scale = CaveScale(layer);
				const Interval tunnel{ -layer.Radius * 0.5f, scale * 2.0f * GradientNoise3DMaxValue - layer.Radius * 0.5f };
				return MaxOf(tunnel, BandRange(layer, boxMin.y, boxMax.y));
			}
			case ESdfTerrainLayerKind::Chambers:
			{
				const float scale = CaveScale(layer);
				const float threshold = 1.0f - layer.Openness;
				const Interval chamber{ scale * (threshold - NoiseHighest(layer.bRidged)), scale * (threshold - NoiseLowest(layer.bRidged)) };
				return MaxOf(chamber, BandRange(layer, boxMin.y, boxMax.y));
			}
			case ESdfTerrainLayerKind::Detail:
				return Scaled(layer.Amplitude, -GradientNoise3DMaxValue, GradientNoise3DMaxValue);
			}
			return {};
		}

		void SampleBlock(const SdfTerrainComponent& terrain, const Block& block, float* outValues, uint32_t* outMaterials)
		{
			std::array<float, BlockSize> layerValues;
			for (size_t i = 0; i < block.Count; ++i)
			{
				outValues[i] = SdfTerrainEmptyValue;
				outMaterials[i] = 0;
			}

			for (const SdfTerrainLayer& layer : terrain.Layers)
			{
				if (!IsLayerActive(layer))
				{
					continue;
				}
				SampleLayer(terrain, layer, block, layerValues.data());

				if (layer.Kind == ESdfTerrainLayerKind::Detail)
				{
					for (size_t i = 0; i < block.Count; ++i)
					{
						outValues[i] = outValues[i] + layerValues[i];
					}
					continue;
				}

				const float radius = layer.BlendRadius;
				switch (layer.Combine)
				{
				case ESdfTerrainCombine::Add:
					for (size_t i = 0; i < block.Count; ++i)
					{
						if (layerValues[i] < outValues[i])
						{
							outMaterials[i] = layer.Material;
						}
						outValues[i] = SmoothMinimum(outValues[i], layerValues[i], radius);
					}
					break;
				case ESdfTerrainCombine::Carve:
					for (size_t i = 0; i < block.Count; ++i)
					{
						outValues[i] = SmoothMaximum(outValues[i], -layerValues[i], radius);
					}
					break;
				case ESdfTerrainCombine::Intersect:
					for (size_t i = 0; i < block.Count; ++i)
					{
						if (layerValues[i] > outValues[i])
						{
							outMaterials[i] = layer.Material;
						}
						outValues[i] = SmoothMaximum(outValues[i], layerValues[i], radius);
					}
					break;
				}
			}
		}
	}

	void SampleTerrainBatch(const SdfTerrainComponent& terrain, const float* x, const float* y, const float* z, size_t count, float* outValues,
		uint32_t* outMaterials)
	{
		std::array<uint32_t, BlockSize> materials;
		for (size_t first = 0; first < count; first += BlockSize)
		{
			const Block block{ x + first, y + first, z + first, std::min(BlockSize, count - first) };
			SampleBlock(terrain, block, outValues + first, outMaterials != nullptr ? outMaterials + first : materials.data());
		}
	}

	TerrainSample SampleTerrain(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		TerrainSample sample;
		SampleTerrainBatch(terrain, &point.x, &point.y, &point.z, 1, &sample.Value, &sample.Material);
		return sample;
	}

	float SampleTerrainValue(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		return SampleTerrain(terrain, point).Value;
	}

	FieldGrid SampleTerrainGrid(const SdfTerrainComponent& terrain, const glm::vec3& origin, float cellSize, const glm::ivec3& size)
	{
		FieldGrid grid;
		grid.Size = glm::max(size, glm::ivec3(0));
		grid.Origin = origin;
		grid.CellSize = cellSize;
		const size_t count = size_t(grid.Size.x) * size_t(grid.Size.y) * size_t(grid.Size.z);
		std::vector<float> x(count);
		std::vector<float> y(count);
		std::vector<float> z(count);
		for (int k = 0; k < grid.Size.z; ++k)
		{
			for (int j = 0; j < grid.Size.y; ++j)
			{
				for (int i = 0; i < grid.Size.x; ++i)
				{
					// From whole numbers of cells, so a sample is at the same place whichever grid
					// asks for it
					const size_t index = grid.IndexOf(i, j, k);
					x[index] = origin.x + float(i) * cellSize;
					y[index] = origin.y + float(j) * cellSize;
					z[index] = origin.z + float(k) * cellSize;
				}
			}
		}
		grid.Values.resize(count);
		grid.Materials.resize(count);
		SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), count, grid.Values.data(), grid.Materials.data());
		return grid;
	}

	float GetTerrainMaxSlope(const SdfTerrainComponent& terrain)
	{
		float slope = 0.0f;
		for (const SdfTerrainLayer& layer : terrain.Layers)
		{
			if (!IsLayerActive(layer))
			{
				continue;
			}
			const float own = LayerSlope(terrain, layer);
			// A sum moves as fast as both parts; a minimum or maximum, smooth or not, as fast as the
			// faster
			slope = layer.Kind == ESdfTerrainLayerKind::Detail ? slope + own : std::max(slope, own);
		}
		return slope;
	}

	TerrainValueRange GetTerrainValueRange(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		Interval range{ SdfTerrainEmptyValue, SdfTerrainEmptyValue };
		for (const SdfTerrainLayer& layer : terrain.Layers)
		{
			if (!IsLayerActive(layer))
			{
				continue;
			}
			const Interval own = LayerRange(layer, boxMin, boxMax);
			const float offset = SmoothMinimumMaxOffset(layer.BlendRadius);
			if (layer.Kind == ESdfTerrainLayerKind::Detail)
			{
				range = { range.Low + own.Low, range.High + own.High };
				continue;
			}
			switch (layer.Combine)
			{
			case ESdfTerrainCombine::Add:
				range = { std::min(range.Low, own.Low) - offset, std::min(range.High, own.High) };
				break;
			case ESdfTerrainCombine::Carve:
				range = { std::max(range.Low, -own.High), std::max(range.High, -own.Low) + offset };
				break;
			case ESdfTerrainCombine::Intersect:
				range = { std::max(range.Low, own.Low), std::max(range.High, own.High) + offset };
				break;
			}
		}

		// The value at the centre, widened by how far it can change to the corners
		const glm::vec3 center = (boxMin + boxMax) * 0.5f;
		const float reach = GetTerrainMaxSlope(terrain) * glm::length(boxMax - center);
		const float atCenter = SampleTerrainValue(terrain, center);
		const Interval around{ atCenter - reach, atCenter + reach };

		// Both hold, so the tighter of each end does; a little wider for rounding in the arithmetic
		TerrainValueRange result;
		const float low = std::max(range.Low, around.Low);
		const float high = std::min(range.High, around.High);
		const float margin = 1e-4f * std::max({ 1.0f, std::fabs(low), std::fabs(high) });
		result.Lowest = low - margin;
		result.Highest = std::max(high, low) + margin;
		return result;
	}

	glm::vec3 GetTerrainNormal(const SdfTerrainComponent& terrain, const glm::vec3& point, float step)
	{
		const float xs[6] = { point.x + step, point.x - step, point.x, point.x, point.x, point.x };
		const float ys[6] = { point.y, point.y, point.y + step, point.y - step, point.y, point.y };
		const float zs[6] = { point.z, point.z, point.z, point.z, point.z + step, point.z - step };
		float values[6] = {};
		SampleTerrainBatch(terrain, xs, ys, zs, 6, values);
		const glm::vec3 gradient(values[0] - values[1], values[2] - values[3], values[4] - values[5]);
		const float length = glm::length(gradient);
		return length > 1e-12f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
	}

	std::optional<TerrainRayHit> RayCastTerrain(const SdfTerrainComponent& terrain, const glm::vec3& origin, const glm::vec3& direction,
		float maxDistance)
	{
		const float slope = GetTerrainMaxSlope(terrain);
		const float length = glm::length(direction);
		if (!(slope > 0.0f) || !(length > 0.0f) || !(maxDistance >= 0.0f))
		{
			return std::nullopt;
		}
		const glm::vec3 along = direction / length;

		auto hitAt = [&](float distance)
		{
			TerrainRayHit hit;
			hit.Distance = distance;
			hit.Point = origin + along * distance;
			const TerrainSample sample = SampleTerrain(terrain, hit.Point);
			hit.Material = sample.Material;
			hit.Normal = GetTerrainNormal(terrain, hit.Point);
			return hit;
		};

		constexpr float MinStep = 0.01f;
		float value = SampleTerrainValue(terrain, origin);
		if (value <= 0.0f)
		{
			return hitAt(0.0f);
		}

		float distance = 0.0f;
		while (distance < maxDistance)
		{
			const float before = distance;
			distance = std::min(distance + std::max(value / slope, MinStep), maxDistance);
			value = SampleTerrainValue(terrain, origin + along * distance);
			if (value <= 0.0f)
			{
				// Outside at `outside`, inside at `inside`: halve the step until it is well under a
				// millimetre, and hit at the inside end
				float outside = before;
				float inside = distance;
				for (int halving = 0; halving < 24 && inside - outside > 1e-4f; ++halving)
				{
					const float middle = (outside + inside) * 0.5f;
					if (SampleTerrainValue(terrain, origin + along * middle) <= 0.0f)
					{
						inside = middle;
					}
					else
					{
						outside = middle;
					}
				}
				return hitAt(inside);
			}
		}
		return std::nullopt;
	}

	std::optional<glm::vec3> FindTerrainFreePoint(const SdfTerrainComponent& terrain, const glm::vec3& point, float clearance, float maxDistance)
	{
		const float radius = std::max(clearance, 0.0f);
		// The 26 ways from a cube's middle to its faces, edges and corners, made unit: written out, the
		// same bits everywhere
		std::vector<glm::vec3> around;
		for (int z = -1; z <= 1; ++z)
		{
			for (int y = -1; y <= 1; ++y)
			{
				for (int x = -1; x <= 1; ++x)
				{
					const int count = std::abs(x) + std::abs(y) + std::abs(z);
					if (count == 0)
					{
						continue;
					}
					const float scale = count == 1 ? 1.0f : (count == 2 ? 0.70710677f : 0.57735026f);
					around.push_back(glm::vec3(float(x), float(y), float(z)) * scale);
				}
			}
		}
		std::vector<float> x(around.size() + 1);
		std::vector<float> y(around.size() + 1);
		std::vector<float> z(around.size() + 1);
		std::vector<float> values(around.size() + 1);
		auto isFree = [&](const glm::vec3& at)
		{
			x[0] = at.x;
			y[0] = at.y;
			z[0] = at.z;
			for (size_t n = 0; n < around.size(); ++n)
			{
				const glm::vec3 probe = at + around[n] * radius;
				x[n + 1] = probe.x;
				y[n + 1] = probe.y;
				z[n + 1] = probe.z;
			}
			SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), values.size(), values.data());
			return std::all_of(values.begin(), values.end(), [](float value) { return value >= 0.0f; });
		};

		glm::vec3 at = point;
		if (isFree(at))
		{
			return at;
		}
		const float step = std::clamp(radius * 0.1f, 0.01f, 0.25f);
		for (float travelled = step; travelled <= maxDistance; travelled += step)
		{
			at += GetTerrainNormal(terrain, at) * step;
			if (isFree(at))
			{
				return at;
			}
		}
		return std::nullopt;
	}
}

#include "NyxPCH.h"
#include "SdfTerrain.h"

#include "GradientNoise.h"
#include "SmoothMinimum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

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

		// The sine and cosine of an angle in degrees, by a fixed polynomial in doubles: only arithmetic
		// that IEEE 754 rounds the same everywhere, where a C library's sin() may round its own way
		void SinCosDegrees(float degrees, float& outSine, float& outCosine)
		{
			// To a multiple of 90 degrees and a remainder of at most 45, in radians
			const double turns = std::floor((double(degrees) + 45.0) / 90.0);
			const double x = (double(degrees) - turns * 90.0) * 0.017453292519943295;
			const double x2 = x * x;
			const double sine = x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0 + x2 * (-1.0 / 39916800.0))))));
			const double cosine = 1.0 + x2 * (-0.5 + x2 * (1.0 / 24.0 + x2 * (-1.0 / 720.0 + x2 * (1.0 / 40320.0 + x2 * (-1.0 / 3628800.0)))));
			const int quarter = int(turns - 4.0 * std::floor(turns / 4.0));
			const double sines[4] = { sine, cosine, -sine, -cosine };
			const double cosines[4] = { cosine, -sine, -cosine, sine };
			outSine = float(sines[quarter]);
			outCosine = float(cosines[quarter]);
		}

		// From the terrain's space into a box's own: the inverse of its rotation about X, then Y, then Z
		glm::mat3 BoxInverseRotation(const glm::vec3& rotation)
		{
			float sx, cx, sy, cy, sz, cz;
			SinCosDegrees(rotation.x, sx, cx);
			SinCosDegrees(rotation.y, sy, cy);
			SinCosDegrees(rotation.z, sz, cz);
			// Columns: each axis of the box in the terrain's space, Z * Y * X
			const glm::mat3 aboutX(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, cx, sx), glm::vec3(0.0f, -sx, cx));
			const glm::mat3 aboutY(glm::vec3(cy, 0.0f, -sy), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(sy, 0.0f, cy));
			const glm::mat3 aboutZ(glm::vec3(cz, sz, 0.0f), glm::vec3(-sz, cz, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			return glm::transpose(aboutZ * aboutY * aboutX);
		}

		// The distance to a placed shape, a layer's or an edit's, negative inside: exact, so its slope
		// is 1
		struct ShapeDistance
		{
			enum class EKind : uint8_t
			{
				Box,
				Sphere,
				Capsule,
				None
			};

			explicit ShapeDistance(const SdfTerrainLayer& layer) : Position(layer.Position), End(layer.End), Radius(layer.Radius)
			{
				switch (layer.Kind)
				{
				case ESdfTerrainLayerKind::Box:
					SetBox(layer.HalfSize, layer.Rotation, layer.Rounding);
					break;
				case ESdfTerrainLayerKind::Sphere:
					Kind = EKind::Sphere;
					break;
				case ESdfTerrainLayerKind::Capsule:
					Kind = EKind::Capsule;
					break;
				default:
					break;
				}
			}

			explicit ShapeDistance(const SdfTerrainEdit& edit) : Position(edit.Position), End(edit.End), Radius(edit.Radius)
			{
				switch (edit.Shape)
				{
				case ESdfTerrainEditShape::Box:
					SetBox(edit.HalfSize, edit.Rotation, 0.0f);
					break;
				case ESdfTerrainEditShape::Sphere:
					Kind = EKind::Sphere;
					break;
				case ESdfTerrainEditShape::Capsule:
					Kind = EKind::Capsule;
					break;
				}
			}

			void SetBox(const glm::vec3& halfSize, const glm::vec3& rotation, float rounding)
			{
				Kind = EKind::Box;
				ToBox = BoxInverseRotation(rotation);
				Rounding = std::clamp(rounding, 0.0f, std::min({ halfSize.x, halfSize.y, halfSize.z }));
				Inner = glm::max(halfSize - Rounding, glm::vec3(0.0f));
			}

			float At(const glm::vec3& point) const
			{
				switch (Kind)
				{
				case EKind::Box:
				{
					const glm::vec3 local = ToBox * (point - Position);
					const glm::vec3 q = glm::abs(local) - Inner;
					const float outside = glm::length(glm::max(q, glm::vec3(0.0f)));
					const float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
					return outside + inside - Rounding;
				}
				case EKind::Sphere:
					return glm::length(point - Position) - Radius;
				case EKind::Capsule:
				{
					const glm::vec3 along = End - Position;
					const float lengthSquared = glm::dot(along, along);
					const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - Position, along) / lengthSquared, 0.0f, 1.0f) : 0.0f;
					return glm::length(point - (Position + along * t)) - Radius;
				}
				default:
					return SdfTerrainEmptyValue;
				}
			}

			// What it can be in a box: within half the box's diagonal of its value at the box's middle,
			// widened by a hair for the rounding of that value
			Interval Range(const glm::vec3& boxMin, const glm::vec3& boxMax) const
			{
				const float value = At((boxMin + boxMax) * 0.5f);
				const float reach = glm::length(boxMax - boxMin) * 0.5f;
				const float margin = 1e-4f * (1.0f + std::fabs(value) + reach);
				return { value - reach - margin, value + reach + margin };
			}

			EKind Kind = EKind::None;
			glm::vec3 Position{ 0.0f };
			glm::vec3 End{ 0.0f };
			float Radius = 0.0f;
			glm::mat3 ToBox{ 1.0f };
			float Rounding = 0.0f;
			glm::vec3 Inner{ 0.0f };
		};

		// One block of points: what one layer's own shape is there (for Detail, how far it pushes)
		struct Block
		{
			const float* X = nullptr;
			const float* Y = nullptr;
			const float* Z = nullptr;
			size_t Count = 0;
		};

		// A noise over X and Z alone (at Y = 0) for every point of a block, each different place once:
		// a brick's points share their X and Z four times over. Places are told apart by their bits, so
		// each gets exactly the value it would on its own.
		void SampleFlatNoise(const FractalNoise3D& noise, bool bRidged, const Block& block, float* out)
		{
			static const std::array<float, BlockSize> Zeros{};
			std::array<float, BlockSize> x;
			std::array<float, BlockSize> z;
			std::array<uint64_t, BlockSize> places;
			std::array<uint8_t, BlockSize> placeOf;
			size_t count = 0;
			for (size_t i = 0; i < block.Count; ++i)
			{
				uint32_t xBits = 0;
				uint32_t zBits = 0;
				std::memcpy(&xBits, &block.X[i], sizeof(float));
				std::memcpy(&zBits, &block.Z[i], sizeof(float));
				const uint64_t place = uint64_t(xBits) | (uint64_t(zBits) << 32);
				size_t found = 0;
				while (found < count && places[found] != place)
				{
					++found;
				}
				if (found == count)
				{
					places[count] = place;
					x[count] = block.X[i];
					z[count] = block.Z[i];
					++count;
				}
				placeOf[i] = static_cast<uint8_t>(found);
			}
			std::array<float, BlockSize> values;
			SampleNoise(noise, bRidged, x.data(), Zeros.data(), z.data(), count, values.data());
			for (size_t i = 0; i < block.Count; ++i)
			{
				out[i] = values[placeOf[i]];
			}
		}

		void SampleLayer(const SdfTerrainComponent& terrain, const SdfTerrainLayer& layer, const Block& block, float* out)
		{
			std::array<float, BlockSize> first;
			std::array<float, BlockSize> second;
			std::array<float, BlockSize> third;

			switch (layer.Kind)
			{
			case ESdfTerrainLayerKind::Heightfield:
			{
				SampleFlatNoise(MakeNoise(terrain, layer, ENoiseRole::Main), layer.bRidged, block, first.data());
				for (size_t i = 0; i < block.Count; ++i)
				{
					const float height = Terrace(layer, layer.Height + layer.Amplitude * first[i]);
					out[i] = block.Y[i] - height;
				}
				break;
			}
			case ESdfTerrainLayerKind::Landmass:
			{
				SampleFlatNoise(MakeNoise(terrain, layer, ENoiseRole::Main), layer.bRidged, block, first.data());
				SampleFlatNoise(MakeNoise(terrain, layer, ENoiseRole::Second), true, block, second.data());
				SampleFlatNoise(MakeNoise(terrain, layer, ENoiseRole::Third), false, block, third.data());
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
			case ESdfTerrainLayerKind::Box:
			case ESdfTerrainLayerKind::Sphere:
			case ESdfTerrainLayerKind::Capsule:
			{
				const ShapeDistance shape(layer);
				for (size_t i = 0; i < block.Count; ++i)
				{
					out[i] = shape.At(glm::vec3(block.X[i], block.Y[i], block.Z[i]));
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
			case ESdfTerrainLayerKind::Box:
			case ESdfTerrainLayerKind::Sphere:
			case ESdfTerrainLayerKind::Capsule:
				return 1.0f;
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
			case ESdfTerrainLayerKind::Box:
			case ESdfTerrainLayerKind::Sphere:
			case ESdfTerrainLayerKind::Capsule:
				return ShapeDistance(layer).Range(boxMin, boxMax);
			}
			return {};
		}

		// Whether joining a layer whose value lies within `layer` over a block can change values that lie
		// within [lowest, highest] there: false only where the join is proven to give each value back
		// exactly, so skipping it is the same bits. A smooth join returns the plain one exactly where the
		// two are at least its radius apart (SmoothMinimum()), so the layer must be that far off, with a
		// margin for the rounding of the comparison.
		bool CanChange(ESdfTerrainCombine combine, float blendRadius, const Interval& range, float lowest, float highest)
		{
			const float radius = std::max(blendRadius, 0.0f);
			auto clear = [&](float apart, float a, float b) { return apart > radius + 1e-5f * (1.0f + std::fabs(a) + std::fabs(b) + radius); };
			switch (combine)
			{
			case ESdfTerrainCombine::Add:
				// The minimum keeps what was there while the layer is above it
				return !clear(range.Low - highest, range.Low, highest);
			case ESdfTerrainCombine::Carve:
				// The maximum with the layer's negative keeps it while that is below it
				return !clear(range.Low + lowest, range.Low, lowest);
			case ESdfTerrainCombine::Intersect:
				return !clear(lowest - range.High, lowest, range.High);
			}
			return true;
		}

		// What the layers can make the value in a box, by the interval arithmetic of their ranges
		Interval LayersRange(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
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
			return range;
		}

		// The same for an edit: a dig as a carve, a fill as an add; paint changes no value, only the
		// material where its shape is
		bool CanEditChange(const SdfTerrainEdit& edit, const Interval& range, float lowest, float highest)
		{
			switch (edit.Mode)
			{
			case ESdfTerrainEditMode::Subtract:
				return CanChange(ESdfTerrainCombine::Carve, edit.BlendRadius, range, lowest, highest);
			case ESdfTerrainEditMode::Add:
				return CanChange(ESdfTerrainCombine::Add, edit.BlendRadius, range, lowest, highest);
			case ESdfTerrainEditMode::Paint:
				return range.Low < 0.0f;
			}
			return true;
		}

		// What an edit makes of values within `range`, as the interval arithmetic of the ranges does it
		Interval EditInterval(const SdfTerrainEdit& edit, const Interval& own, const Interval& range)
		{
			const float offset = SmoothMinimumMaxOffset(edit.BlendRadius);
			switch (edit.Mode)
			{
			case ESdfTerrainEditMode::Subtract:
				return { std::max(range.Low, -own.High), std::max(range.High, -own.Low) + offset };
			case ESdfTerrainEditMode::Add:
				return { std::min(range.Low, own.Low) - offset, std::min(range.High, own.High) };
			case ESdfTerrainEditMode::Paint:
				return range;
			}
			return range;
		}

		void SampleBlock(const SdfTerrainComponent& terrain, std::span<const SdfTerrainEdit> edits, const Block& block, float* outValues, uint32_t* outMaterials)
		{
			std::array<float, BlockSize> layerValues;
			for (size_t i = 0; i < block.Count; ++i)
			{
				outValues[i] = SdfTerrainEmptyValue;
				outMaterials[i] = 0;
			}

			// The block's bounds, and what its values are between so far, to leave out the layers that
			// can't change them: a shape far off, a landmass beside it
			glm::vec3 boxMin(std::numeric_limits<float>::max());
			glm::vec3 boxMax(-std::numeric_limits<float>::max());
			for (size_t i = 0; i < block.Count; ++i)
			{
				const glm::vec3 point(block.X[i], block.Y[i], block.Z[i]);
				boxMin = glm::min(boxMin, point);
				boxMax = glm::max(boxMax, point);
			}
			float lowest = SdfTerrainEmptyValue;
			float highest = SdfTerrainEmptyValue;

			for (const SdfTerrainLayer& layer : terrain.Layers)
			{
				if (!IsLayerActive(layer))
				{
					continue;
				}
				if (layer.Kind != ESdfTerrainLayerKind::Detail && !CanChange(layer.Combine, layer.BlendRadius, LayerRange(layer, boxMin, boxMax), lowest, highest))
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
				}
				else
				{
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
				lowest = outValues[0];
				highest = outValues[0];
				for (size_t i = 1; i < block.Count; ++i)
				{
					lowest = std::min(lowest, outValues[i]);
					highest = std::max(highest, outValues[i]);
				}
			}

			// Then the edits, in the order of their revisions, those that can't change the block left out
			for (const SdfTerrainEdit& edit : edits)
			{
				const ShapeDistance shape(edit);
				if (!CanEditChange(edit, shape.Range(boxMin, boxMax), lowest, highest))
				{
					continue;
				}
				const float radius = edit.BlendRadius;
				for (size_t i = 0; i < block.Count; ++i)
				{
					const float value = shape.At(glm::vec3(block.X[i], block.Y[i], block.Z[i]));
					switch (edit.Mode)
					{
					case ESdfTerrainEditMode::Subtract:
						outValues[i] = SmoothMaximum(outValues[i], -value, radius);
						break;
					case ESdfTerrainEditMode::Add:
						if (value < outValues[i])
						{
							outMaterials[i] = edit.Material;
						}
						outValues[i] = SmoothMinimum(outValues[i], value, radius);
						break;
					case ESdfTerrainEditMode::Paint:
						if (value < 0.0f)
						{
							outMaterials[i] = edit.Material;
						}
						break;
					}
				}
				lowest = outValues[0];
				highest = outValues[0];
				for (size_t i = 1; i < block.Count; ++i)
				{
					lowest = std::min(lowest, outValues[i]);
					highest = std::max(highest, outValues[i]);
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
			SampleBlock(terrain, terrain.Edits, block, outValues + first, outMaterials != nullptr ? outMaterials + first : materials.data());
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

	FieldGrid SampleTerrainGrid(const SdfTerrainComponent& terrain, const glm::vec3& origin, float cellSize, const glm::ivec3& size,
		const FieldRunParts& runParts)
	{
		FieldGrid grid;
		grid.Size = glm::max(size, glm::ivec3(0));
		grid.Origin = origin;
		grid.CellSize = cellSize;
		const size_t count = size_t(grid.Size.x) * size_t(grid.Size.y) * size_t(grid.Size.z);
		grid.Values.resize(count);
		grid.Materials.resize(count);

		// In bricks of 4 x 4 x 4, a block each, so a block's bounds are tight and leave out the layers
		// that can't reach it; a slab of bricks a part
		auto slab = [&](size_t part)
		{
			std::array<float, BlockSize> x;
			std::array<float, BlockSize> y;
			std::array<float, BlockSize> z;
			std::array<float, BlockSize> values;
			std::array<uint32_t, BlockSize> materials;
			std::array<size_t, BlockSize> indices;
			const int brickZ = int(part) * 4;
			for (int brickY = 0; brickY < grid.Size.y; brickY += 4)
			{
				for (int brickX = 0; brickX < grid.Size.x; brickX += 4)
				{
					size_t n = 0;
					for (int k = brickZ; k < std::min(brickZ + 4, grid.Size.z); ++k)
					{
						for (int j = brickY; j < std::min(brickY + 4, grid.Size.y); ++j)
						{
							for (int i = brickX; i < std::min(brickX + 4, grid.Size.x); ++i)
							{
								// From whole numbers of cells, so a sample is at the same place whichever
								// grid asks for it
								x[n] = origin.x + float(i) * cellSize;
								y[n] = origin.y + float(j) * cellSize;
								z[n] = origin.z + float(k) * cellSize;
								indices[n] = grid.IndexOf(i, j, k);
								++n;
							}
						}
					}
					SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), n, values.data(), materials.data());
					for (size_t m = 0; m < n; ++m)
					{
						grid.Values[indices[m]] = values[m];
						grid.Materials[indices[m]] = materials[m];
					}
				}
			}
		};
		const size_t slabs = size_t((grid.Size.z + 3) / 4);
		if (runParts && slabs > 1)
		{
			runParts(slabs, slab);
		}
		else
		{
			for (size_t part = 0; part < slabs; ++part)
			{
				slab(part);
			}
		}
		return grid;
	}

	namespace
	{
		float LayersSlope(const SdfTerrainComponent& terrain)
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
	}

	float GetTerrainMaxSlope(const SdfTerrainComponent& terrain)
	{
		// Every edit's shape is a distance, which changes a metre a metre at most
		const float slope = LayersSlope(terrain);
		return terrain.Edits.empty() ? slope : std::max(slope, 1.0f);
	}

	TerrainValueRange GetTerrainValueRange(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		Interval range = LayersRange(terrain, boxMin, boxMax);
		for (const SdfTerrainEdit& edit : terrain.Edits)
		{
			range = EditInterval(edit, ShapeDistance(edit).Range(boxMin, boxMax), range);
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

	uint64_t AppendTerrainEdit(SdfTerrainComponent& terrain, SdfTerrainEdit edit)
	{
		edit.Revision = GetTerrainRevision(terrain) + 1;
		terrain.Edits.push_back(edit);
		return edit.Revision;
	}

	uint64_t GetTerrainRevision(const SdfTerrainComponent& terrain)
	{
		return terrain.Edits.empty() ? 0 : terrain.Edits.back().Revision;
	}

	namespace
	{
		constexpr uint64_t EditHashStart = 0xCBF29CE484222325ull;

		void HashEdit(uint64_t& hash, const SdfTerrainEdit& edit)
		{
			auto add = [&](const void* data, size_t size)
			{
				const auto* bytes = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					hash = (hash ^ bytes[i]) * 0x100000001B3ull;
				}
			};
			add(&edit.Revision, sizeof(edit.Revision));
			add(&edit.Shape, sizeof(edit.Shape));
			add(&edit.Mode, sizeof(edit.Mode));
			add(&edit.Position, sizeof(edit.Position));
			add(&edit.End, sizeof(edit.End));
			add(&edit.HalfSize, sizeof(edit.HalfSize));
			add(&edit.Rotation, sizeof(edit.Rotation));
			add(&edit.Radius, sizeof(edit.Radius));
			add(&edit.BlendRadius, sizeof(edit.BlendRadius));
			add(&edit.Material, sizeof(edit.Material));
		}

		bool IsSameEdit(const SdfTerrainEdit& a, const SdfTerrainEdit& b)
		{
			return a.Revision == b.Revision && a.Shape == b.Shape && a.Mode == b.Mode && a.Position == b.Position && a.End == b.End &&
				   a.HalfSize == b.HalfSize && a.Rotation == b.Rotation && a.Radius == b.Radius && a.BlendRadius == b.BlendRadius &&
				   a.Material == b.Material;
		}

		// An edit's shape's box, its blend left out
		std::pair<glm::vec3, glm::vec3> ShapeBounds(const SdfTerrainEdit& edit)
		{
			SdfTerrainEdit sharp = edit;
			sharp.BlendRadius = 0.0f;
			sharp.Mode = ESdfTerrainEditMode::Subtract;
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainEditSignBounds(sharp, low, high);
			return { low, high };
		}

		// Into the box's range and reach
		void TakeEdit(const SdfTerrainEdit& edit, const Interval& own, Interval& range, TerrainBoxEdits& box, size_t index)
		{
			range = EditInterval(edit, own, range);
			box.Reach.Revision = std::max(box.Reach.Revision, edit.Revision);
			HashEdit(box.Reach.Hash, edit);
			++box.Reach.Count;
			box.Reaching.push_back(static_cast<uint32_t>(index));
			box.RangesAfter.push_back({ range.Low, range.High });
			box.ReachesAfter.push_back(box.Reach);
		}

		// The edits from `box.Seen` on that can change a value in its box, each given to `visit`, the
		// box's range carried past each. With an index, only those it finds near enough: an edit
		// reaches the box only if its shape's distance from the box's centre is within the half
		// diagonal, the farthest the values are from 0 and its blend radius (its range over the box
		// is that distance less the half diagonal, against the values less its blend radius), and the
		// distance to a shape is never less than to its box. The values widen as edits are taken: past
		// what the index was asked for, it is asked again from there.
		template <typename Visit>
		void ContinueBox(const SdfTerrainComponent& terrain, TerrainBoxEdits& box, const TerrainEditIndex* index, Visit&& visit)
		{
			Interval range{ box.Range.Lowest, box.Range.Highest };
			auto consider = [&](size_t at)
			{
				const SdfTerrainEdit& edit = terrain.Edits[at];
				const Interval own = ShapeDistance(edit).Range(box.BoxMin, box.BoxMax);
				if (CanEditChange(edit, own, range.Low, range.High))
				{
					TakeEdit(edit, own, range, box, at);
					visit(edit);
				}
			};
			// A few edits are quicker looked at than asked of the index
			constexpr size_t FewEdits = 64;
			if (index == nullptr || index->GetCount() != terrain.Edits.size() || terrain.Edits.size() - std::min(box.Seen, terrain.Edits.size()) <= FewEdits)
			{
				for (size_t at = box.Seen; at < terrain.Edits.size(); ++at)
				{
					consider(at);
				}
			}
			else
			{
				const glm::vec3 center = (box.BoxMin + box.BoxMax) * 0.5f;
				const float half = glm::length(box.BoxMax - box.BoxMin) * 0.5f;
				std::vector<uint32_t> near;
				size_t from = box.Seen;
				while (from < terrain.Edits.size())
				{
					const float farthest = std::max({ -range.Low, range.High, 0.0f });
					const float reach = half + farthest + index->GetMaxBlendRadius();
					const float asked = reach + 1e-3f * (1.0f + reach);
					near.clear();
					index->Find(center - asked, center + asked, from, near);
					size_t next = terrain.Edits.size();
					for (const uint32_t at : near)
					{
						consider(at);
						if (std::max({ -range.Low, range.High, 0.0f }) > farthest)
						{
							next = size_t(at) + 1;
							break;
						}
					}
					from = next;
				}
			}
			box.Seen = terrain.Edits.size();
			box.Range.Lowest = range.Low;
			box.Range.Highest = range.High;
		}
	}

	void TerrainEditIndex::Update(const SdfTerrainComponent& terrain, size_t same)
	{
		auto key = [](const glm::ivec3& cell)
		{
			auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 20)) & 0x1FFFFFu); };
			return part(cell.x) | (part(cell.y) << 21) | (part(cell.z) << 42);
		};
		// Those from `same` on go; each cell's are in order, so they are its last
		same = std::min(same, Bounds.size());
		if (same == 0)
		{
			Cells.clear();
			Large.clear();
			Bounds.clear();
			MaxBlendRadius = 0.0f;
		}
		while (Bounds.size() > same)
		{
			const uint32_t at = static_cast<uint32_t>(Bounds.size() - 1);
			const auto [low, high] = Bounds.back();
			if (!Large.empty() && Large.back() == at)
			{
				Large.pop_back();
			}
			else
			{
				for (int z = low.z; z <= high.z; ++z)
				{
					for (int y = low.y; y <= high.y; ++y)
					{
						for (int x = low.x; x <= high.x; ++x)
						{
							const auto cell = Cells.find(key({ x, y, z }));
							if (cell != Cells.end() && !cell->second.empty() && cell->second.back() == at)
							{
								cell->second.pop_back();
							}
						}
					}
				}
			}
			Bounds.pop_back();
		}
		for (size_t at = Bounds.size(); at < terrain.Edits.size(); ++at)
		{
			const SdfTerrainEdit& edit = terrain.Edits[at];
			const auto [shapeLow, shapeHigh] = ShapeBounds(edit);
			const glm::ivec3 low(glm::floor(shapeLow / CellSize));
			const glm::ivec3 high(glm::floor(shapeHigh / CellSize));
			Bounds.push_back({ low, high });
			MaxBlendRadius = std::max(MaxBlendRadius, edit.BlendRadius);
			const glm::ivec3 span = high - low + 1;
			if (int64_t(span.x) * span.y * span.z > MaxCells)
			{
				Large.push_back(static_cast<uint32_t>(at));
				continue;
			}
			for (int z = low.z; z <= high.z; ++z)
			{
				for (int y = low.y; y <= high.y; ++y)
				{
					for (int x = low.x; x <= high.x; ++x)
					{
						Cells[key({ x, y, z })].push_back(static_cast<uint32_t>(at));
					}
				}
			}
		}
	}

	void TerrainEditIndex::Find(const glm::vec3& boxMin, const glm::vec3& boxMax, size_t from, std::vector<uint32_t>& out) const
	{
		auto key = [](const glm::ivec3& cell)
		{
			auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 20)) & 0x1FFFFFu); };
			return part(cell.x) | (part(cell.y) << 21) | (part(cell.z) << 42);
		};
		const size_t first = out.size();
		for (const uint32_t at : Large)
		{
			if (at >= from)
			{
				out.push_back(at);
			}
		}
		const glm::ivec3 low(glm::floor(boxMin / CellSize));
		const glm::ivec3 high(glm::floor(boxMax / CellSize));
		const glm::ivec3 span = high - low + 1;
		if (int64_t(span.x) * span.y * span.z > int64_t(Cells.size()))
		{
			// A box over more cells than there are: through every cell instead
			for (const auto& [cellKey, edits] : Cells)
			{
				const glm::ivec3 cell(int((cellKey & 0x1FFFFF)) - (1 << 20), int(((cellKey >> 21) & 0x1FFFFF)) - (1 << 20), int(((cellKey >> 42) & 0x1FFFFF)) - (1 << 20));
				if (glm::all(glm::greaterThanEqual(cell, low)) && glm::all(glm::lessThanEqual(cell, high)))
				{
					for (auto it = std::lower_bound(edits.begin(), edits.end(), uint32_t(from)); it != edits.end(); ++it)
					{
						out.push_back(*it);
					}
				}
			}
		}
		else
		{
			for (int z = low.z; z <= high.z; ++z)
			{
				for (int y = low.y; y <= high.y; ++y)
				{
					for (int x = low.x; x <= high.x; ++x)
					{
						const auto cell = Cells.find(key({ x, y, z }));
						if (cell == Cells.end())
						{
							continue;
						}
						for (auto it = std::lower_bound(cell->second.begin(), cell->second.end(), uint32_t(from)); it != cell->second.end(); ++it)
						{
							out.push_back(*it);
						}
					}
				}
			}
		}
		std::sort(out.begin() + first, out.end());
		out.erase(std::unique(out.begin() + first, out.end()), out.end());
	}

	size_t CountSameTerrainEdits(const SdfTerrainComponent& terrain, const SdfTerrainComponent& earlier)
	{
		const size_t count = std::min(terrain.Edits.size(), earlier.Edits.size());
		for (size_t at = 0; at < count; ++at)
		{
			if (!IsSameEdit(terrain.Edits[at], earlier.Edits[at]))
			{
				return at;
			}
		}
		return count;
	}

	TerrainBoxEdits BeginTerrainBoxEdits(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		// The layers' range, tightened by the layers' value at the centre widened by their slope to the
		// corners, as GetTerrainValueRange() does for the whole field
		const Interval layers = LayersRange(terrain, boxMin, boxMax);
		const glm::vec3 center = (boxMin + boxMax) * 0.5f;
		float atCenter = SdfTerrainEmptyValue;
		uint32_t material = 0;
		SampleBlock(terrain, {}, Block{ &center.x, &center.y, &center.z, 1 }, &atCenter, &material);
		const float reach = LayersSlope(terrain) * glm::length(boxMax - center);
		const float low = std::max(layers.Low, atCenter - reach);
		const float high = std::min(layers.High, atCenter + reach);
		const float margin = 1e-4f * std::max({ 1.0f, std::fabs(low), std::fabs(high) });

		TerrainBoxEdits box;
		box.BoxMin = boxMin;
		box.BoxMax = boxMax;
		box.Range.Lowest = low - margin;
		box.Range.Highest = std::max(high, low) + margin;
		box.Layers = box.Range;
		box.Reach.Hash = EditHashStart;
		return box;
	}

	void ContinueTerrainBoxEdits(const SdfTerrainComponent& terrain, TerrainBoxEdits& box, const TerrainEditIndex* index)
	{
		ContinueBox(terrain, box, index, [](const SdfTerrainEdit&) {});
	}

	void RewindTerrainBoxEdits(const SdfTerrainComponent& /*terrain*/, TerrainBoxEdits& box, size_t count)
	{
		if (box.Seen <= count)
		{
			return;
		}
		// As it was after the last of the first `count` that reached it: the same arithmetic, in the
		// same order, as continuing over them made
		const size_t kept = size_t(std::lower_bound(box.Reaching.begin(), box.Reaching.end(), uint32_t(std::min<size_t>(count, UINT32_MAX))) - box.Reaching.begin());
		box.Reaching.resize(kept);
		box.RangesAfter.resize(kept);
		box.ReachesAfter.resize(kept);
		box.Range = kept == 0 ? box.Layers : box.RangesAfter.back();
		if (kept == 0)
		{
			box.Reach = {};
			box.Reach.Hash = EditHashStart;
		}
		else
		{
			box.Reach = box.ReachesAfter.back();
		}
		box.Seen = count;
	}

	TerrainEditsReach GetTerrainEditsReaching(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		TerrainBoxEdits box = BeginTerrainBoxEdits(terrain, boxMin, boxMax);
		ContinueTerrainBoxEdits(terrain, box);
		return box.Reach;
	}

	TerrainEditsReach KeepTerrainEditsReaching(SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		TerrainBoxEdits box = BeginTerrainBoxEdits(terrain, boxMin, boxMax);
		std::vector<SdfTerrainEdit> kept;
		ContinueBox(terrain, box, nullptr, [&](const SdfTerrainEdit& edit) { kept.push_back(edit); });
		terrain.Edits = std::move(kept);
		return box.Reach;
	}

	void HashTerrainEditRuns(const SdfTerrainComponent& terrain, std::vector<uint64_t>& hashes)
	{
		if (hashes.empty() || hashes.size() > terrain.Edits.size() + 1)
		{
			hashes.assign(1, EditHashStart);
		}
		for (size_t index = hashes.size() - 1; index < terrain.Edits.size(); ++index)
		{
			uint64_t hash = hashes.back();
			HashEdit(hash, terrain.Edits[index]);
			hashes.push_back(hash);
		}
	}

	uint64_t HashTerrainEdits(const SdfTerrainComponent& terrain)
	{
		uint64_t hash = EditHashStart;
		for (const SdfTerrainEdit& edit : terrain.Edits)
		{
			HashEdit(hash, edit);
		}
		return hash != 0 ? hash : 1;
	}

	bool CanTerrainEditChangeValues(const SdfTerrainEdit& edit, const glm::vec3& boxMin, const glm::vec3& boxMax, float lowest, float highest)
	{
		return CanEditChange(edit, ShapeDistance(edit).Range(boxMin, boxMax), lowest, highest);
	}

	bool CanTerrainEditTurnSign(const SdfTerrainEdit& edit, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		// A smooth join moves a value only within its radius of the other, and by at most a quarter of
		// it: a value crosses 0 only where the shape's distance is within both of 0
		if (edit.Mode == ESdfTerrainEditMode::Paint)
		{
			return false;
		}
		const float radius = std::max(edit.BlendRadius, 0.0f);
		const float reach = radius + SmoothMinimumMaxOffset(radius);
		const Interval own = ShapeDistance(edit).Range(boxMin, boxMax);
		return own.Low < reach + 1e-4f * (1.0f + reach + std::fabs(own.Low));
	}

	bool GetTerrainEditSignBounds(const SdfTerrainEdit& edit, glm::vec3& outMin, glm::vec3& outMax)
	{
		if (edit.Mode == ESdfTerrainEditMode::Paint)
		{
			return false;
		}
		const float blend = std::max(edit.BlendRadius, 0.0f);
		const float reach = blend + SmoothMinimumMaxOffset(blend);
		float around = 0.0f;
		switch (edit.Shape)
		{
		case ESdfTerrainEditShape::Sphere:
			outMin = edit.Position;
			outMax = edit.Position;
			around = std::fabs(edit.Radius);
			break;
		case ESdfTerrainEditShape::Capsule:
			outMin = glm::min(edit.Position, edit.End);
			outMax = glm::max(edit.Position, edit.End);
			around = std::fabs(edit.Radius);
			break;
		case ESdfTerrainEditShape::Box:
			// Turned any way, within its half diagonal of its middle
			outMin = edit.Position;
			outMax = edit.Position;
			around = glm::length(glm::abs(edit.HalfSize));
			break;
		}
		const float margin = 1e-4f * (1.0f + around + reach);
		outMin -= around + reach + margin;
		outMax += around + reach + margin;
		return true;
	}

	bool ExtendsTerrainEdits(const SdfTerrainComponent& terrain, const SdfTerrainComponent& earlier)
	{
		if (terrain.Edits.size() < earlier.Edits.size())
		{
			return false;
		}
		for (size_t index = 0; index < earlier.Edits.size(); ++index)
		{
			if (!IsSameEdit(terrain.Edits[index], earlier.Edits[index]))
			{
				return false;
			}
		}
		return true;
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

#pragma once

#include "FieldMesher.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Nyx
{
	// What an SDF terrain's field guarantees, and what it doesn't (Architecture.md, "Generated
	// content"):
	//
	// - The value at a point is negative inside, positive outside and 0 on the surface. Only its sign
	//   and where it is 0 are exact: it isn't the distance to the surface (y - height is no
	//   distance to a slope).
	// - It changes no faster than its slope bound, GetTerrainMaxSlope(): value / bound is a distance
	//   that is safe to move without crossing the surface. That is what skipping space and casting
	//   rays use. The bounds are proven from each layer's own, so they can be loose, never too small.
	// - Over a box, it lies within GetTerrainValueRange(), so a box whose range is all above 0 is all
	//   air and one all below is all solid.
	// - The same bits on every machine and in every build, one point at a time or many
	//   (SampleTerrainBatch()): only arithmetic IEEE 754 rounds the same everywhere, in a fixed
	//   order.
	//
	// Every point is in the terrain entity's space, in metres.

	// Where no layer made anything: air, this far from anything
	constexpr float SdfTerrainEmptyValue = 100000.0f;

	struct TerrainSample
	{
		float Value = SdfTerrainEmptyValue;
		// The material of the layer whose surface is nearest there (SdfTerrainLayer::Material)
		uint32_t Material = 0;
	};

	TerrainSample SampleTerrain(const SdfTerrainComponent& terrain, const glm::vec3& point);
	float SampleTerrainValue(const SdfTerrainComponent& terrain, const glm::vec3& point);

	// `count` points as three arrays into `outValues` and, if given, `outMaterials`: the same bits as
	// SampleTerrain() point by point, in batches of noise eight at a time where the processor can
	void SampleTerrainBatch(const SdfTerrainComponent& terrain, const float* x, const float* y, const float* z, size_t count, float* outValues,
		uint32_t* outMaterials = nullptr);

	// The terrain on a grid of `size` samples `cellSize` apart from `origin` (FieldGrid), values and
	// materials, in one batch: what a mesh of it is made from (MakeFieldMesh())
	FieldGrid SampleTerrainGrid(const SdfTerrainComponent& terrain, const glm::vec3& origin, float cellSize, const glm::ivec3& size);

	// The most the value can change a metre, in any direction; 0 for a terrain with no layers
	float GetTerrainMaxSlope(const SdfTerrainComponent& terrain);

	// The lowest and highest the value can be anywhere in the box: the tighter of what each layer's
	// reach allows (its heights, its noise's amplitude) and the value at the box's centre widened by
	// the slope bound times half the box's diagonal. Costs one sample.
	struct TerrainValueRange
	{
		float Lowest = SdfTerrainEmptyValue;
		float Highest = SdfTerrainEmptyValue;

		bool IsAllAir() const { return Lowest > 0.0f; }
		bool IsAllSolid() const { return Highest < 0.0f; }
	};
	TerrainValueRange GetTerrainValueRange(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax);

	// Which way is out at the point: the value's gradient, by central differences `step` metres
	// either side, made unit length; up where the value doesn't change
	glm::vec3 GetTerrainNormal(const SdfTerrainComponent& terrain, const glm::vec3& point, float step = 0.05f);

	struct TerrainRayHit
	{
		// Along the ray, in metres from its origin
		float Distance = 0.0f;
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		uint32_t Material = 0;
	};

	// Where a ray first meets the surface, within `maxDistance`: sphere tracing by the slope bound,
	// never less than 1 cm a step, then halving the last step to the crossing. A ray starting inside
	// hits at once. It can step past a wall thinner than a centimetre, never a thicker one.
	std::optional<TerrainRayHit> RayCastTerrain(const SdfTerrainComponent& terrain, const glm::vec3& origin, const glm::vec3& direction,
		float maxDistance);
}

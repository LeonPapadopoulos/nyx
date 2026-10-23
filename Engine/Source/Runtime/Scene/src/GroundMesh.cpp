#include "NyxPCH.h"
#include "GroundMesh.h"

#include "Profiler.h"
#include "RuntimeMeshComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace Nyx
{
	namespace
	{
		constexpr uint32_t MaxCellsPerSide = 512;

		// A lattice point's own number: integer arithmetic only, the same everywhere
		uint32_t HashLattice(int32_t x, int32_t z, uint32_t seed)
		{
			uint32_t hash = seed * 0x9E3779B1u;
			hash ^= static_cast<uint32_t>(x) * 0x85EBCA77u;
			hash = (hash ^ (hash >> 16)) * 0x7FEB352Du;
			hash ^= static_cast<uint32_t>(z) * 0xC2B2AE3Du;
			hash = (hash ^ (hash >> 15)) * 0x846CA68Bu;
			return hash ^ (hash >> 16);
		}

		// The lattice point's gradient, one of eight along the axes and diagonals, against the way
		// from it to the point
		float Gradient(uint32_t hash, float x, float z)
		{
			switch (hash & 7u)
			{
			case 0: return x + z;
			case 1: return x - z;
			case 2: return -x + z;
			case 3: return -x - z;
			case 4: return x;
			case 5: return -x;
			case 6: return z;
			default: return -z;
			}
		}

		float Mix(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		// 0 below `from`, 1 above `to`, smooth between
		float SmoothStep(float from, float to, float value)
		{
			if (to <= from)
			{
				return value < from ? 0.0f : 1.0f;
			}
			const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// Gradient noise: smooth hills about -1 to 1, one lattice square a unit, 0 at its corners
		float GradientNoise(float x, float z, uint32_t seed)
		{
			const float cellX = std::floor(x);
			const float cellZ = std::floor(z);
			const int32_t ix = static_cast<int32_t>(cellX);
			const int32_t iz = static_cast<int32_t>(cellZ);
			const float fx = x - cellX;
			const float fz = z - cellZ;
			const float n00 = Gradient(HashLattice(ix, iz, seed), fx, fz);
			const float n10 = Gradient(HashLattice(ix + 1, iz, seed), fx - 1.0f, fz);
			const float n01 = Gradient(HashLattice(ix, iz + 1, seed), fx, fz - 1.0f);
			const float n11 = Gradient(HashLattice(ix + 1, iz + 1, seed), fx - 1.0f, fz - 1.0f);
			// Quintic, so the slope is smooth across the squares' edges too
			const float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
			const float v = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
			return Mix(Mix(n00, n10, u), Mix(n01, n11, u), v);
		}

		// The octaves summed, each finer and fainter, divided by their weights: about -1 to 1. Each
		// octave has its own seed and is shifted, so their lattices' zeros don't line up.
		float LayeredNoise(const GroundComponent& ground, float x, float z)
		{
			const uint32_t octaves = std::clamp(ground.Octaves, 1u, 12u);
			float sum = 0.0f;
			float weights = 0.0f;
			float weight = 1.0f;
			float frequency = ground.Frequency;
			for (uint32_t octave = 0; octave < octaves; ++octave)
			{
				const float shift = 17.31f * static_cast<float>(octave);
				sum += weight * GradientNoise(x * frequency + shift, z * frequency - shift, ground.Seed + octave * 0x632BE5ABu);
				weights += weight;
				weight *= ground.Gain;
				frequency *= ground.Lacunarity;
			}
			return weights > 0.0f ? sum / weights : 0.0f;
		}

		// How far out to the edges, 0 in the middle and 1 at the edge, for the basin
		float BasinDistance(const GroundComponent& ground, float x, float z)
		{
			const float halfX = std::max(ground.Size.x * 0.5f, 0.01f);
			const float halfZ = std::max(ground.Size.y * 0.5f, 0.01f);
			switch (ground.Basin)
			{
			case EGroundBasin::Valley:
				return std::abs(x) / halfX;
			case EGroundBasin::Bowl:
			{
				const float across = x / halfX;
				const float along = z / halfZ;
				return std::sqrt(across * across + along * along);
			}
			default:
				return 0.0f;
			}
		}

		// Steps of `step` metres, the slope between two steps squeezed into a share of it as sharp
		// as asked
		float Terrace(float height, float step, float sharpness)
		{
			const float steps = height / step;
			const float level = std::floor(steps);
			const float within = steps - level;
			const float halfSlope = 0.5f * (1.0f - std::clamp(sharpness, 0.0f, 1.0f));
			const float shaped = halfSlope <= 0.0f ? (within < 0.5f ? 0.0f : 1.0f) : SmoothStep(0.5f - halfSlope, 0.5f + halfSlope, within);
			return (level + shaped) * step;
		}
	}

	float SampleGroundHeight(const GroundComponent& ground, float x, float z)
	{
		// Nearly flat in the middle: a little of the noise is left, so it isn't a floor
		const float distance = std::sqrt(x * x + z * z);
		const float flatness = SmoothStep(ground.FlatRadius, ground.FlatRadius + ground.FlatBlend, distance);
		float height = LayeredNoise(ground, x, z) * ground.Amplitude * (0.15f + 0.85f * flatness);

		height += ground.BasinDepth * SmoothStep(ground.BasinFloor, 1.0f, BasinDistance(ground, x, z));

		if (ground.TerraceHeight > 0.0f)
		{
			height = Terrace(height, ground.TerraceHeight, ground.TerraceSharpness);
		}
		return height;
	}

	glm::vec3 GetGroundColor(const GroundComponent& ground, float height, float slopeDegrees)
	{
		const float blend = std::max(ground.SlopeBlendDegrees, 0.0f) * 0.5f;
		glm::vec3 color = glm::mix(ground.GrassColor, ground.EarthColor, SmoothStep(ground.GrassThinStart, ground.GrassThinEnd, height));
		color = glm::mix(color, ground.EarthColor, SmoothStep(ground.EarthSlopeDegrees - blend, ground.EarthSlopeDegrees + blend, slopeDegrees));
		return glm::mix(color, ground.StoneColor, SmoothStep(ground.StoneSlopeDegrees - blend, ground.StoneSlopeDegrees + blend, slopeDegrees));
	}

	MeshData MakeGroundMesh(const GroundComponent& ground)
	{
		NYX_PROFILE_FUNCTION();
		const glm::vec2 size = glm::max(ground.Size, glm::vec2(0.01f));
		const float spacing = std::max(ground.Spacing, 0.01f);
		const uint32_t cellsX = std::clamp(static_cast<uint32_t>(std::lround(size.x / spacing)), 1u, MaxCellsPerSide);
		const uint32_t cellsZ = std::clamp(static_cast<uint32_t>(std::lround(size.y / spacing)), 1u, MaxCellsPerSide);
		const float stepX = size.x / static_cast<float>(cellsX);
		const float stepZ = size.y / static_cast<float>(cellsZ);

		// The heights with a border of one more all round, so every vertex has neighbours to take its
		// slope from
		const uint32_t rowLength = cellsX + 3;
		std::vector<float> heights(static_cast<size_t>(rowLength) * (cellsZ + 3));
		for (uint32_t row = 0; row < cellsZ + 3; ++row)
		{
			const float z = -0.5f * size.y + (static_cast<float>(row) - 1.0f) * stepZ;
			for (uint32_t column = 0; column < rowLength; ++column)
			{
				const float x = -0.5f * size.x + (static_cast<float>(column) - 1.0f) * stepX;
				heights[static_cast<size_t>(row) * rowLength + column] = SampleGroundHeight(ground, x, z);
			}
		}
		const auto heightAt = [&](uint32_t column, uint32_t row)
		{
			return heights[static_cast<size_t>(row + 1) * rowLength + column + 1];
		};

		MeshData mesh;
		mesh.Vertices.reserve(static_cast<size_t>(cellsX + 1) * (cellsZ + 1));
		for (uint32_t row = 0; row <= cellsZ; ++row)
		{
			const float z = -0.5f * size.y + static_cast<float>(row) * stepZ;
			for (uint32_t column = 0; column <= cellsX; ++column)
			{
				const float x = -0.5f * size.x + static_cast<float>(column) * stepX;
				const float height = heightAt(column, row);
				// How fast the height rises along X and Z, across the neighbours on both sides
				const float alongX = (heights[static_cast<size_t>(row + 1) * rowLength + column + 2] - heights[static_cast<size_t>(row + 1) * rowLength + column]) / (2.0f * stepX);
				const float alongZ = (heights[static_cast<size_t>(row + 2) * rowLength + column + 1] - heights[static_cast<size_t>(row) * rowLength + column + 1]) / (2.0f * stepZ);
				const glm::vec3 normal = glm::normalize(glm::vec3(-alongX, 1.0f, -alongZ));
				// Along rising u, which is X; v rises with Z, so the bitangent, up the image, points to -Z
				const glm::vec3 alongU = glm::vec3(1.0f, alongX, 0.0f);
				const glm::vec3 tangent = glm::normalize(alongU - normal * glm::dot(normal, alongU));
				const float slopeDegrees = glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
				mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(x, height, z),
					.Color = GetGroundColor(ground, height, slopeDegrees),
					.UV = glm::vec2(x, z),
					.Normal = normal,
					.Tangent = glm::vec4(tangent, 1.0f) });
			}
		}

		// Counter-clockwise from above
		mesh.Indices.reserve(static_cast<size_t>(cellsX) * cellsZ * 6);
		for (uint32_t row = 0; row < cellsZ; ++row)
		{
			for (uint32_t column = 0; column < cellsX; ++column)
			{
				const uint32_t nearLeft = (row + 1) * (cellsX + 1) + column;
				const uint32_t farLeft = row * (cellsX + 1) + column;
				mesh.Indices.insert(mesh.Indices.end(), { nearLeft, nearLeft + 1, farLeft + 1, farLeft + 1, farLeft, nearLeft });
			}
		}
		return mesh;
	}

	uint64_t HashGroundValues(const GroundComponent& ground)
	{
		uint64_t hash = 14695981039346656037ull;
		const auto add = [&hash](const auto& value)
		{
			unsigned char bytes[sizeof(value)];
			std::memcpy(bytes, &value, sizeof(value));
			for (const unsigned char byte : bytes)
			{
				hash = (hash ^ byte) * 1099511628211ull;
			}
		};
		for (const float value : { ground.Size.x, ground.Size.y, ground.Spacing, ground.Frequency, ground.Amplitude, ground.Lacunarity,
				 ground.Gain, ground.BasinDepth, ground.BasinFloor, ground.FlatRadius, ground.FlatBlend, ground.TerraceHeight,
				 ground.TerraceSharpness, ground.EarthSlopeDegrees, ground.StoneSlopeDegrees, ground.SlopeBlendDegrees,
				 ground.GrassThinStart, ground.GrassThinEnd })
		{
			add(value);
		}
		for (const glm::vec3& color : { ground.GrassColor, ground.EarthColor, ground.StoneColor })
		{
			add(color.r);
			add(color.g);
			add(color.b);
		}
		add(ground.Seed);
		add(ground.Octaves);
		add(ground.Basin);
		return hash;
	}

	void UpdateGroundMesh(GroundComponent& ground, Registry& world, Entity entity)
	{
		if (!world.IsAlive(entity))
		{
			return;
		}
		const uint64_t values = HashGroundValues(ground);
		if (ground.MadeFrom == values && world.Has<RuntimeMeshComponent>(entity) && world.Get<RuntimeMeshComponent>(entity).Mesh)
		{
			return;
		}

		std::shared_ptr<const MeshData> mesh = std::make_shared<MeshData>(MakeGroundMesh(ground));
		RuntimeMeshComponent& runtimeMesh = world.Has<RuntimeMeshComponent>(entity) ? world.Get<RuntimeMeshComponent>(entity)
																					 : world.Add<RuntimeMeshComponent>(entity);
		SetRuntimeMesh(runtimeMesh, std::move(mesh));
		ground.MadeFrom = values;
	}

	void RemoveGroundMesh(Registry& world, Entity entity)
	{
		world.Remove<RuntimeMeshComponent>(entity);
	}
}

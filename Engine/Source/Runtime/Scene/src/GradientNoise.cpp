#include "NyxPCH.h"
#include "GradientNoise.h"

#include <cmath>
#include <cstdint>

namespace Nyx
{
	namespace
	{
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
	}

	float SampleGradientNoise(float x, float z, uint32_t seed)
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
}

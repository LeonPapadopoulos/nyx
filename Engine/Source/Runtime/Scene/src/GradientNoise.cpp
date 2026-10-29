#include "NyxPCH.h"
#include "GradientNoise.h"
#include "GradientNoise3D.h"

#include <cmath>
#include <cstdint>

#if defined(_MSC_VER) && defined(_M_X64)
	#include <intrin.h>
#endif

// Every operation as written, never fused into a multiply-add: the eight-at-a-time noise must give the
// same bits
#pragma fp_contract(off)

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

	float SampleGradientNoise3D(float x, float y, float z, uint32_t seed)
	{
		using namespace Detail;
		const float cellX = std::floor(x);
		const float cellY = std::floor(y);
		const float cellZ = std::floor(z);
		const int32_t ix = static_cast<int32_t>(cellX);
		const int32_t iy = static_cast<int32_t>(cellY);
		const int32_t iz = static_cast<int32_t>(cellZ);
		const float fx = x - cellX;
		const float fy = y - cellY;
		const float fz = z - cellZ;
		const float gx = fx - 1.0f;
		const float gy = fy - 1.0f;
		const float gz = fz - 1.0f;

		const float n000 = Gradient3D(HashLattice3D(ix, iy, iz, seed), fx, fy, fz);
		const float n100 = Gradient3D(HashLattice3D(ix + 1, iy, iz, seed), gx, fy, fz);
		const float n010 = Gradient3D(HashLattice3D(ix, iy + 1, iz, seed), fx, gy, fz);
		const float n110 = Gradient3D(HashLattice3D(ix + 1, iy + 1, iz, seed), gx, gy, fz);
		const float n001 = Gradient3D(HashLattice3D(ix, iy, iz + 1, seed), fx, fy, gz);
		const float n101 = Gradient3D(HashLattice3D(ix + 1, iy, iz + 1, seed), gx, fy, gz);
		const float n011 = Gradient3D(HashLattice3D(ix, iy + 1, iz + 1, seed), fx, gy, gz);
		const float n111 = Gradient3D(HashLattice3D(ix + 1, iy + 1, iz + 1, seed), gx, gy, gz);

		const float u = Fade(fx);
		const float v = Fade(fy);
		const float w = Fade(fz);
		const float nearSum = Lerp(Lerp(n000, n100, u), Lerp(n010, n110, u), v);
		const float farSum = Lerp(Lerp(n001, n101, u), Lerp(n011, n111, u), v);
		return Lerp(nearSum, farSum, w);
	}

	namespace
	{
		float SampleOctaves(const Detail::NoiseOctaves& octaves, bool bRidged, float x, float y, float z)
		{
			float sum = 0.0f;
			for (uint32_t index = 0; index < octaves.Count; ++index)
			{
				const Detail::NoiseOctave& octave = octaves.Octaves[index];
				float value = SampleGradientNoise3D(x * octave.Frequency + octave.ShiftX, y * octave.Frequency + octave.ShiftY,
					z * octave.Frequency + octave.ShiftZ, octave.Seed);
				if (bRidged)
				{
					value = std::max(1.0f - std::fabs(value), 0.0f);
					value = value * value;
				}
				sum = sum + octave.Weight * value;
			}
			return sum / octaves.WeightSum;
		}

		float GetOctavesMaxSlope(const FractalNoise3D& noise, float octaveSlope)
		{
			const Detail::NoiseOctaves octaves = Detail::GetNoiseOctaves(noise);
			float slope = 0.0f;
			for (uint32_t index = 0; index < octaves.Count; ++index)
			{
				slope += octaves.Octaves[index].Weight * std::fabs(octaves.Octaves[index].Frequency) * octaveSlope;
			}
			return slope / octaves.WeightSum;
		}

		bool DetectAvx2()
		{
#if defined(_MSC_VER) && defined(_M_X64)
			int registers[4] = {};
			__cpuid(registers, 0);
			if (registers[0] < 7)
			{
				return false;
			}
			__cpuid(registers, 1);
			const bool bOsSaves = (registers[2] & (1 << 27)) != 0;
			const bool bAvx = (registers[2] & (1 << 28)) != 0;
			if (!bOsSaves || !bAvx || (_xgetbv(0) & 6u) != 6u)
			{
				return false;
			}
			__cpuidex(registers, 7, 0);
			return (registers[1] & (1 << 5)) != 0;
#else
			return false;
#endif
		}

		void SampleFractalBatch(const FractalNoise3D& noise, bool bRidged, const float* x, const float* y, const float* z, size_t count,
			float* outValues)
		{
			const Detail::NoiseOctaves octaves = Detail::GetNoiseOctaves(noise);
			size_t first = 0;
			if (IsNoiseBatchSimd())
			{
				first = count - count % 8;
				Detail::SampleFractalNoise3DAvx2(octaves, bRidged, x, y, z, first, outValues);
			}
			for (size_t index = first; index < count; ++index)
			{
				outValues[index] = SampleOctaves(octaves, bRidged, x[index], y[index], z[index]);
			}
		}
	}

	float SampleFractalNoise3D(const FractalNoise3D& noise, const glm::vec3& point)
	{
		return SampleOctaves(Detail::GetNoiseOctaves(noise), false, point.x, point.y, point.z);
	}

	float SampleRidgedNoise3D(const FractalNoise3D& noise, const glm::vec3& point)
	{
		return SampleOctaves(Detail::GetNoiseOctaves(noise), true, point.x, point.y, point.z);
	}

	float GetFractalNoise3DMaxSlope(const FractalNoise3D& noise)
	{
		return GetOctavesMaxSlope(noise, GradientNoise3DMaxSlope);
	}

	float GetRidgedNoise3DMaxSlope(const FractalNoise3D& noise)
	{
		// d/dx of max(1 - |n|, 0)^2 is at most 2 * |dn/dx|
		return GetOctavesMaxSlope(noise, 2.0f * GradientNoise3DMaxSlope);
	}

	bool IsNoiseBatchSimd()
	{
		static const bool bSimd = DetectAvx2();
		return bSimd;
	}

	void SampleGradientNoise3DBatch(const float* x, const float* y, const float* z, size_t count, uint32_t seed, float* outValues)
	{
		size_t first = 0;
		if (IsNoiseBatchSimd())
		{
			first = count - count % 8;
			Detail::SampleGradientNoise3DAvx2(x, y, z, first, seed, outValues);
		}
		for (size_t index = first; index < count; ++index)
		{
			outValues[index] = SampleGradientNoise3D(x[index], y[index], z[index], seed);
		}
	}

	void SampleFractalNoise3DBatch(const FractalNoise3D& noise, const float* x, const float* y, const float* z, size_t count, float* outValues)
	{
		SampleFractalBatch(noise, false, x, y, z, count, outValues);
	}

	void SampleRidgedNoise3DBatch(const FractalNoise3D& noise, const float* x, const float* y, const float* z, size_t count, float* outValues)
	{
		SampleFractalBatch(noise, true, x, y, z, count, outValues);
	}
}

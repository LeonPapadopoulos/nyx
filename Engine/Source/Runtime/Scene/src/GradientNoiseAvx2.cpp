// The 3D noise eight points at a time, built with AVX2 (CMake gives this file /arch:AVX2 and no
// precompiled header) and called only where the processor has it (IsNoiseBatchSimd()). Every step is
// the one-at-a-time code's (GradientNoise.cpp, GradientNoise3D.h), in the same order, with
// operations IEEE 754 rounds the same in a lane as alone: add, subtract, multiply, divide, floor,
// max, selects and integer arithmetic. Nothing is fused into a multiply-add.
#include "GradientNoise3D.h"

#if defined(_M_X64) || defined(__x86_64__)

	#include <immintrin.h>

	#pragma fp_contract(off)

namespace Nyx::Detail
{
	namespace
	{
		__m256i Mul(__m256i a, uint32_t b)
		{
			return _mm256_mullo_epi32(a, _mm256_set1_epi32(static_cast<int32_t>(b)));
		}

		__m256i XorShift(__m256i a, int shift)
		{
			return _mm256_xor_si256(a, _mm256_srli_epi32(a, shift));
		}

		// HashLattice3D()
		__m256i HashLattice(__m256i x, __m256i y, __m256i z, __m256i seedTerm)
		{
			__m256i hash = seedTerm;
			hash = _mm256_xor_si256(hash, Mul(x, NoiseHashX));
			hash = Mul(XorShift(hash, 16), NoiseMixA);
			hash = _mm256_xor_si256(hash, Mul(y, NoiseHashY));
			hash = Mul(XorShift(hash, 15), NoiseMixB);
			hash = _mm256_xor_si256(hash, Mul(z, NoiseHashZ));
			hash = Mul(XorShift(hash, 16), NoiseMixC);
			return XorShift(hash, 15);
		}

		// Gradient3D(): selects, and the sign flipped by the hash's bits, as negating does
		__m256 Gradient(__m256i hash, __m256 x, __m256 y, __m256 z)
		{
			const __m256i h = _mm256_and_si256(hash, _mm256_set1_epi32(15));
			const __m256 below8 = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(8), h));
			const __m256 below4 = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(4), h));
			const __m256 twelveOr14 = _mm256_castsi256_ps(
				_mm256_or_si256(_mm256_cmpeq_epi32(h, _mm256_set1_epi32(12)), _mm256_cmpeq_epi32(h, _mm256_set1_epi32(14))));

			const __m256 u = _mm256_blendv_ps(y, x, below8);
			const __m256 v = _mm256_blendv_ps(_mm256_blendv_ps(z, x, twelveOr14), y, below4);
			const __m256 signU = _mm256_castsi256_ps(_mm256_slli_epi32(_mm256_and_si256(h, _mm256_set1_epi32(1)), 31));
			const __m256 signV = _mm256_castsi256_ps(_mm256_slli_epi32(_mm256_and_si256(h, _mm256_set1_epi32(2)), 30));
			return _mm256_add_ps(_mm256_xor_ps(u, signU), _mm256_xor_ps(v, signV));
		}

		// Fade()
		__m256 FadeLanes(__m256 t)
		{
			const __m256 inner = _mm256_add_ps(_mm256_mul_ps(t, _mm256_sub_ps(_mm256_mul_ps(t, _mm256_set1_ps(6.0f)), _mm256_set1_ps(15.0f))),
				_mm256_set1_ps(10.0f));
			return _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(t, t), t), inner);
		}

		// Lerp()
		__m256 MixLanes(__m256 a, __m256 b, __m256 t)
		{
			return _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), t));
		}

		// SampleGradientNoise3D()
		__m256 Noise(__m256 x, __m256 y, __m256 z, uint32_t seed)
		{
			const __m256 cellX = _mm256_floor_ps(x);
			const __m256 cellY = _mm256_floor_ps(y);
			const __m256 cellZ = _mm256_floor_ps(z);
			const __m256i ix = _mm256_cvttps_epi32(cellX);
			const __m256i iy = _mm256_cvttps_epi32(cellY);
			const __m256i iz = _mm256_cvttps_epi32(cellZ);
			const __m256i one = _mm256_set1_epi32(1);
			const __m256i jx = _mm256_add_epi32(ix, one);
			const __m256i jy = _mm256_add_epi32(iy, one);
			const __m256i jz = _mm256_add_epi32(iz, one);

			const __m256 fx = _mm256_sub_ps(x, cellX);
			const __m256 fy = _mm256_sub_ps(y, cellY);
			const __m256 fz = _mm256_sub_ps(z, cellZ);
			const __m256 unit = _mm256_set1_ps(1.0f);
			const __m256 gx = _mm256_sub_ps(fx, unit);
			const __m256 gy = _mm256_sub_ps(fy, unit);
			const __m256 gz = _mm256_sub_ps(fz, unit);

			const __m256i seedTerm = _mm256_set1_epi32(static_cast<int32_t>(seed * NoiseHashSeed));
			const __m256 n000 = Gradient(HashLattice(ix, iy, iz, seedTerm), fx, fy, fz);
			const __m256 n100 = Gradient(HashLattice(jx, iy, iz, seedTerm), gx, fy, fz);
			const __m256 n010 = Gradient(HashLattice(ix, jy, iz, seedTerm), fx, gy, fz);
			const __m256 n110 = Gradient(HashLattice(jx, jy, iz, seedTerm), gx, gy, fz);
			const __m256 n001 = Gradient(HashLattice(ix, iy, jz, seedTerm), fx, fy, gz);
			const __m256 n101 = Gradient(HashLattice(jx, iy, jz, seedTerm), gx, fy, gz);
			const __m256 n011 = Gradient(HashLattice(ix, jy, jz, seedTerm), fx, gy, gz);
			const __m256 n111 = Gradient(HashLattice(jx, jy, jz, seedTerm), gx, gy, gz);

			const __m256 u = FadeLanes(fx);
			const __m256 v = FadeLanes(fy);
			const __m256 w = FadeLanes(fz);
			const __m256 nearLanes = MixLanes(MixLanes(n000, n100, u), MixLanes(n010, n110, u), v);
			const __m256 farLanes = MixLanes(MixLanes(n001, n101, u), MixLanes(n011, n111, u), v);
			return MixLanes(nearLanes, farLanes, w);
		}
	}

	void SampleGradientNoise3DAvx2(const float* x, const float* y, const float* z, size_t count, uint32_t seed, float* outValues)
	{
		for (size_t index = 0; index + 8 <= count; index += 8)
		{
			const __m256 value = Noise(_mm256_loadu_ps(x + index), _mm256_loadu_ps(y + index), _mm256_loadu_ps(z + index), seed);
			_mm256_storeu_ps(outValues + index, value);
		}
	}

	void SampleFractalNoise3DAvx2(const NoiseOctaves& octaves, bool bRidged, const float* x, const float* y, const float* z, size_t count,
		float* outValues)
	{
		const __m256 signBit = _mm256_set1_ps(-0.0f);
		const __m256 zero = _mm256_setzero_ps();
		const __m256 unit = _mm256_set1_ps(1.0f);
		for (size_t index = 0; index + 8 <= count; index += 8)
		{
			const __m256 px = _mm256_loadu_ps(x + index);
			const __m256 py = _mm256_loadu_ps(y + index);
			const __m256 pz = _mm256_loadu_ps(z + index);
			__m256 sum = zero;
			for (uint32_t octaveIndex = 0; octaveIndex < octaves.Count; ++octaveIndex)
			{
				const NoiseOctave& octave = octaves.Octaves[octaveIndex];
				const __m256 frequency = _mm256_set1_ps(octave.Frequency);
				__m256 value = Noise(_mm256_add_ps(_mm256_mul_ps(px, frequency), _mm256_set1_ps(octave.ShiftX)),
					_mm256_add_ps(_mm256_mul_ps(py, frequency), _mm256_set1_ps(octave.ShiftY)),
					_mm256_add_ps(_mm256_mul_ps(pz, frequency), _mm256_set1_ps(octave.ShiftZ)), octave.Seed);
				if (bRidged)
				{
					value = _mm256_max_ps(_mm256_sub_ps(unit, _mm256_andnot_ps(signBit, value)), zero);
					value = _mm256_mul_ps(value, value);
				}
				sum = _mm256_add_ps(sum, _mm256_mul_ps(_mm256_set1_ps(octave.Weight), value));
			}
			_mm256_storeu_ps(outValues + index, _mm256_div_ps(sum, _mm256_set1_ps(octaves.WeightSum)));
		}
	}
}

#else

namespace Nyx::Detail
{
	// No AVX2 here: IsNoiseBatchSimd() is false, and these are never called
	void SampleGradientNoise3DAvx2(const float*, const float*, const float*, size_t, uint32_t, float*) {}
	void SampleFractalNoise3DAvx2(const NoiseOctaves&, bool, const float*, const float*, const float*, size_t, float*) {}
}

#endif

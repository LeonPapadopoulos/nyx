#pragma once

// What the one-at-a-time and the eight-at-a-time 3D noise share, so they can't drift apart: the
// hash, the gradients, the fade and each octave's numbers. Inside Scene/src only.

#include "GradientNoise.h"

#include <algorithm>
#include <cstdint>

namespace Nyx::Detail
{
	constexpr uint32_t MaxNoiseOctaves = 16;

	constexpr uint32_t NoiseHashSeed = 0x9E3779B1u;
	constexpr uint32_t NoiseHashX = 0x85EBCA77u;
	constexpr uint32_t NoiseHashY = 0xC2B2AE3Du;
	constexpr uint32_t NoiseHashZ = 0x27D4EB2Fu;
	constexpr uint32_t NoiseMixA = 0x7FEB352Du;
	constexpr uint32_t NoiseMixB = 0x846CA68Bu;
	constexpr uint32_t NoiseMixC = 0x165667B1u;

	// A lattice point's own number: integer arithmetic only, the same everywhere
	inline uint32_t HashLattice3D(int32_t x, int32_t y, int32_t z, uint32_t seed)
	{
		uint32_t hash = seed * NoiseHashSeed;
		hash ^= static_cast<uint32_t>(x) * NoiseHashX;
		hash = (hash ^ (hash >> 16)) * NoiseMixA;
		hash ^= static_cast<uint32_t>(y) * NoiseHashY;
		hash = (hash ^ (hash >> 15)) * NoiseMixB;
		hash ^= static_cast<uint32_t>(z) * NoiseHashZ;
		hash = (hash ^ (hash >> 16)) * NoiseMixC;
		return hash ^ (hash >> 15);
	}

	// One of the twelve gradients towards a cube's edges (four of them twice, so sixteen to pick
	// from by the hash's low bits) against the way (x, y, z) from the lattice point: Perlin's
	// improved noise
	inline float Gradient3D(uint32_t hash, float x, float y, float z)
	{
		const uint32_t h = hash & 15u;
		const float u = h < 8u ? x : y;
		const float v = h < 4u ? y : (h == 12u || h == 14u ? x : z);
		return ((h & 1u) != 0u ? -u : u) + ((h & 2u) != 0u ? -v : v);
	}

	inline float Fade(float t)
	{
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	inline float Lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	// Each octave's numbers, worked out once for both ways of sampling
	struct NoiseOctave
	{
		float Frequency = 1.0f;
		float Weight = 1.0f;
		// Added after scaling, so the octaves' lattices don't line up
		float ShiftX = 0.0f;
		float ShiftY = 0.0f;
		float ShiftZ = 0.0f;
		uint32_t Seed = 0;
	};

	struct NoiseOctaves
	{
		NoiseOctave Octaves[MaxNoiseOctaves];
		uint32_t Count = 0;
		float WeightSum = 0.0f;
	};

	inline NoiseOctaves GetNoiseOctaves(const FractalNoise3D& noise)
	{
		NoiseOctaves octaves;
		octaves.Count = std::clamp(noise.Octaves, 1u, MaxNoiseOctaves);
		float frequency = noise.Frequency;
		float weight = 1.0f;
		for (uint32_t index = 0; index < octaves.Count; ++index)
		{
			NoiseOctave& octave = octaves.Octaves[index];
			octave.Frequency = frequency;
			octave.Weight = weight;
			const float shift = static_cast<float>(index) * 19.19f;
			octave.ShiftX = shift;
			octave.ShiftY = -shift;
			octave.ShiftZ = shift * 0.5f;
			octave.Seed = noise.Seed + index * 0x632BE5ABu;
			octaves.WeightSum += weight;
			frequency *= noise.Lacunarity;
			weight *= noise.Gain;
		}
		return octaves;
	}

	// The eight-at-a-time versions (GradientNoiseAvx2.cpp), for a multiple of eight points; only
	// called where IsNoiseBatchSimd()
	void SampleGradientNoise3DAvx2(const float* x, const float* y, const float* z, size_t count, uint32_t seed, float* outValues);
	void SampleFractalNoise3DAvx2(const NoiseOctaves& octaves, bool bRidged, const float* x, const float* y, const float* z, size_t count,
		float* outValues);
}

#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace Nyx
{
	// Gradient noise over a plane: smooth hills about -1 to 1, one lattice square a unit, 0 at its
	// corners, as many above 0 as below. From integer hashes and arithmetic that IEEE 754 rounds the
	// same everywhere, so the same on every machine: what a ground's heights and a scatter's noise
	// mask are made from.
	float SampleGradientNoise(float x, float z, uint32_t seed);

	// Gradient noise through space, the same kind in 3D: one lattice cube a unit, 0 at its corners,
	// a gradient at each corner picked by a hash from the twelve towards the cube's edges, blended by
	// a quintic fade, so its slope is smooth across the cubes' faces too. Mostly within -1 to 1; at
	// most GradientNoise3DMaxValue either way, and never steeper than GradientNoise3DMaxSlope. Only
	// additions, subtractions, multiplications, floor and integer hashes, in a fixed order: the same
	// bits on every machine, in every build, one point at a time or eight
	// (SampleGradientNoise3DBatch()). Coordinates within ±2^24, where floats are whole numbers apart
	// at most a unit.
	float SampleGradientNoise3D(float x, float y, float z, uint32_t seed);

	// What the noise can't exceed, proven rather than measured: its value is a blend, by weights
	// that add up to 1, of the corners' gradients against the way to the point, each at most 2 (a
	// gradient has two components of ±1 and the way is within a unit cube)
	constexpr float GradientNoise3DMaxValue = 2.0f;
	// Its slope, a bound on how much it changes a unit, proven the same way: along each axis at most
	// 1 from the gradients plus 1.875 (the fade's steepest) times 4 (two corners' values apart),
	// 8.5, so 8.5 * sqrt(3) in any direction. Loose: the steepest measured is far less
	// (NyxGradientNoiseTests prints it), which a later bound may use once it is proven.
	constexpr float GradientNoise3DMaxSlope = 14.73f;

	// Octaves of 3D noise added up, each `Lacunarity` times finer and `Gain` times fainter than the
	// one before, the first `Frequency` lattice cubes a unit. Each octave has a seed of its own and
	// is shifted, so the octaves' lattices don't line up.
	struct FractalNoise3D
	{
		uint32_t Seed = 1;
		// 1 to 16
		uint32_t Octaves = 5;
		float Frequency = 0.02f;
		float Lacunarity = 2.0f;
		float Gain = 0.5f;
	};

	// Fractal Brownian motion: the octaves added, each weighted, the sum divided by the weights' sum,
	// so it is about -1 to 1 like one octave, at most GradientNoise3DMaxValue either way
	float SampleFractalNoise3D(const FractalNoise3D& noise, const glm::vec3& point);

	// Ridged noise: each octave folded to 1 - |n|, clamped at 0 and squared, so where an octave
	// crosses 0 it makes a sharp crest; added and divided as above, from 0 to 1, crests near 1
	float SampleRidgedNoise3D(const FractalNoise3D& noise, const glm::vec3& point);

	// The most either can change a unit: the octaves' slopes, each its weight times its frequency
	// times GradientNoise3DMaxSlope (ridged: twice that, for the square), over the weights' sum
	float GetFractalNoise3DMaxSlope(const FractalNoise3D& noise);
	float GetRidgedNoise3DMaxSlope(const FractalNoise3D& noise);

	// The same, for `count` points given as three arrays, into `outValues`: eight points at a time
	// with AVX2 where the processor has it, the rest one at a time, every value the same bits as
	// one call of the function above for the point
	void SampleGradientNoise3DBatch(const float* x, const float* y, const float* z, size_t count, uint32_t seed, float* outValues);
	void SampleFractalNoise3DBatch(const FractalNoise3D& noise, const float* x, const float* y, const float* z, size_t count, float* outValues);
	void SampleRidgedNoise3DBatch(const FractalNoise3D& noise, const float* x, const float* y, const float* z, size_t count, float* outValues);

	// Whether the batches go eight points at a time here (AVX2 and an operating system that keeps
	// its registers), for the tests and the stats
	bool IsNoiseBatchSimd();
}

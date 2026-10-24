#pragma once

#include <cstdint>

namespace Nyx
{
	// Gradient noise over a plane: smooth hills about -1 to 1, one lattice square a unit, 0 at its
	// corners, as many above 0 as below. From integer hashes and arithmetic that IEEE 754 rounds the
	// same everywhere, so the same on every machine: what a ground's heights and a scatter's noise
	// mask are made from.
	float SampleGradientNoise(float x, float z, uint32_t seed);
}

#pragma once

#include <algorithm>
#include <cmath>

namespace Nyx
{
	// The smaller of a and b, the corner between them rounded over `radius`: the polynomial
	// (quadratic) smooth minimum, min(a, b) - h * h * radius / 4 with h = max(radius - |a - b|, 0) /
	// radius. Where a and b are at least `radius` apart it is exactly the minimum; elsewhere it is
	// below it by at most radius / 4 (SmoothMinimumMaxOffset()). It changes no faster than a or b
	// does, so two fields' slope bounds hold for their blend. Only arithmetic IEEE 754 rounds the same
	// everywhere: the same bits on every machine. A radius of 0 or less: the plain minimum.
	inline float SmoothMinimum(float a, float b, float radius)
	{
		const float lower = std::min(a, b);
		if (!(radius > 0.0f))
		{
			return lower;
		}
		const float h = std::max(radius - std::fabs(a - b), 0.0f) / radius;
		return lower - h * h * radius * 0.25f;
	}

	// The larger, its corner rounded the same way, above the maximum by at most radius / 4
	inline float SmoothMaximum(float a, float b, float radius)
	{
		return -SmoothMinimum(-a, -b, radius);
	}

	// How far a smooth minimum or maximum can be from the plain one
	constexpr float SmoothMinimumMaxOffset(float radius)
	{
		return radius > 0.0f ? radius * 0.25f : 0.0f;
	}
}

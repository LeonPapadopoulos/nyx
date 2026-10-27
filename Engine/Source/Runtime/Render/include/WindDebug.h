#pragma once

#include "DebugDraw.h"
#include "Wind.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// The wind's debug view (WindComponent's Debug View), as lines a frame round a camera at
	// `camera`, at `groundHeight` and a little above, at the world's `time`. Reads only the wind's
	// contract.

	// How far round the camera the arrows reach, and the crests of the fronts, in metres
	inline constexpr float WindDebugArrowReach = 24.0f;
	inline constexpr float WindDebugFrontReach = 60.0f;
	// Metres between the arrows, and how long an arrow is a metre a second of wind
	inline constexpr float WindDebugArrowSpacing = 3.0f;
	inline constexpr float WindDebugArrowSecondsLong = 0.5f;

	// An arrow at every point of a grid on the ground, along the wind there and as long as it is
	// fast, from blue in a lull to red at a front's peak; with `bGustFronts`, the crest of each front
	// within reach, a line across the wind, a stroke up from it every few metres as tall as it is
	// strong there
	void DrawWindDebug(DebugDraw& drawing, const WindState& wind, const glm::vec3& camera, float groundHeight, float time, bool bGustFronts);
}

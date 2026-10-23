#pragma once

namespace Nyx
{
	class FixedStepClock;

	// The time of what moves by itself, such as a decal's pulse (SceneUBO::Time), in a world drawn
	// between its simulation's steps by `clock` (IRenderer::SetWorldInterpolation()): the world's
	// time, in seconds, at the moment its poses are drawn at (FixedStepClock::GetDrawFactor()).
	// Paused, it stands still, and Step moves it on by one step.
	float GetDrawnWorldTime(const FixedStepClock& clock);
}

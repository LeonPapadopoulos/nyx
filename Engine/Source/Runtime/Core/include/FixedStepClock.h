#pragma once

#include "NyxEngineAPI.h"

#include <cstdint>

namespace Nyx
{
	// One step of the simulation, as a layer's OnFixedUpdate sees it
	struct StepTime
	{
		// The step's length in seconds
		double FixedDeltaSeconds = 0.0;
		// Which step this is, from 0
		uint64_t StepIndex = 0;
		// The world's time in seconds when the step starts
		double WorldTimeSeconds = 0.0;
	};

	// Turns the real time of each frame into whole steps of the same length, so the simulation runs
	// the same however fast the frames come. What is left over waits for the next frame, and tells
	// how far the frame is into the next step. Knows nothing of windows or layers.
	class NYXENGINE_API FixedStepClock
	{
	public:
		static constexpr double DefaultStepSeconds = 1.0 / 60.0;
		// Real time per frame beyond this is ignored, so a hitch or a breakpoint doesn't make the
		// world race to catch up.
		static constexpr double MaxFrameSeconds = 0.25;
		static constexpr uint32_t MaxStepsPerFrame = 4;
		static constexpr double MinTimeScale = 0.1;
		static constexpr double MaxTimeScale = 4.0;

		// Adds a frame's real time and returns how many steps to run for it: at most
		// MaxStepsPerFrame, the time beyond that dropped. While paused, 0, or 1 after StepOnce().
		uint32_t Advance(double realSeconds);

		// The step `step` (from 0) of those the last Advance() asked for
		StepTime GetFrameStep(uint32_t step) const;

		// The length of a step; values of 0 or less are ignored
		void SetStepSeconds(double stepSeconds);
		double GetStepSeconds() const { return StepSeconds; }

		// How far the frame is into the next step, in [0, 1): for drawing between two steps
		double GetInterpolationFactor() const;

		// Steps run so far, and the world time they make up
		uint64_t GetStepIndex() const { return StepIndex; }
		double GetWorldTimeSeconds() const;

		// While paused, real time doesn't count and no steps run.
		void SetPaused(bool paused);
		bool IsPaused() const { return Paused; }
		// While paused, makes the next Advance() run exactly one step; otherwise does nothing
		void StepOnce();

		// Multiplies the real time added, clamped to [MinTimeScale, MaxTimeScale]
		void SetTimeScale(double timeScale);
		double GetTimeScale() const { return TimeScale; }

		// Whether the last Advance() dropped time, beyond MaxFrameSeconds or MaxStepsPerFrame, and how
		// much world time has been dropped in all
		bool IsDroppingTime() const { return DroppingTime; }
		double GetDroppedSeconds() const { return DroppedSeconds; }

	private:
		double StepSeconds = DefaultStepSeconds;
		double Accumulator = 0.0;
		uint64_t StepIndex = 0;

		// The world time at the step where the step length last changed, so changing it doesn't
		// move the time already passed
		uint64_t BaseStepIndex = 0;
		double BaseWorldTimeSeconds = 0.0;

		// The first step of those the last Advance() asked for
		uint64_t FrameFirstStep = 0;

		bool Paused = false;
		bool StepOncePending = false;
		double TimeScale = 1.0;

		bool DroppingTime = false;
		double DroppedSeconds = 0.0;
	};
}

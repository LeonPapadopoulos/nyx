#include "FixedStepClock.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	uint32_t FixedStepClock::Advance(double realSeconds)
	{
		FrameFirstStep = StepIndex;
		DroppingTime = false;

		if (Paused)
		{
			if (!StepOncePending)
			{
				return 0;
			}
			StepOncePending = false;
			++StepIndex;
			return 1;
		}

		const double frameSeconds = std::clamp(realSeconds, 0.0, MaxFrameSeconds);
		double droppedSeconds = (std::max(realSeconds, 0.0) - frameSeconds) * TimeScale;
		Accumulator += frameSeconds * TimeScale;

		// A step that is due but for rounding still runs: a second of frames at 144 fps adds up to
		// a hair under 60 steps.
		const double tolerance = StepSeconds * 1e-6;
		const double dueSteps = std::floor((Accumulator + tolerance) / StepSeconds);
		Accumulator = std::max(Accumulator - dueSteps * StepSeconds, 0.0);

		const uint32_t steps = static_cast<uint32_t>(std::min(dueSteps, static_cast<double>(MaxStepsPerFrame)));
		droppedSeconds += (dueSteps - steps) * StepSeconds;
		StepIndex += steps;

		if (droppedSeconds > 0.0)
		{
			DroppingTime = true;
			DroppedSeconds += droppedSeconds;
		}
		return steps;
	}

	StepTime FixedStepClock::GetFrameStep(uint32_t step) const
	{
		const uint64_t stepIndex = FrameFirstStep + step;
		return StepTime{ StepSeconds, stepIndex, BaseWorldTimeSeconds + static_cast<double>(stepIndex - BaseStepIndex) * StepSeconds };
	}

	void FixedStepClock::SetStepSeconds(double stepSeconds)
	{
		if (stepSeconds <= 0.0)
		{
			return;
		}
		BaseWorldTimeSeconds = GetWorldTimeSeconds();
		BaseStepIndex = StepIndex;
		StepSeconds = stepSeconds;
	}

	double FixedStepClock::GetInterpolationFactor() const
	{
		// Below 1 even if rounding left the accumulator a hair over a step
		return std::min(Accumulator / StepSeconds, std::nextafter(1.0, 0.0));
	}

	double FixedStepClock::GetWorldTimeSeconds() const
	{
		return BaseWorldTimeSeconds + static_cast<double>(StepIndex - BaseStepIndex) * StepSeconds;
	}

	void FixedStepClock::SetPaused(bool paused)
	{
		Paused = paused;
		// A step asked for in one pause doesn't carry over into the next
		StepOncePending = false;
	}

	void FixedStepClock::StepOnce()
	{
		if (Paused)
		{
			StepOncePending = true;
		}
	}

	void FixedStepClock::SetTimeScale(double timeScale)
	{
		TimeScale = std::clamp(timeScale, MinTimeScale, MaxTimeScale);
	}
}

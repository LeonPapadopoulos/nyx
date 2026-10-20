// The fixed simulation tick (FixedStepClock): frames of real time into whole steps, the cap on steps
// per frame, pause and step, time scale, and the step's index and world time.
#include "FixedStepClock.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using Nyx::FixedStepClock;
	using Nyx::StepTime;

	using Nyx::Tests::Require;

	constexpr double Step = FixedStepClock::DefaultStepSeconds;

	bool Near(double a, double b)
	{
		return std::abs(a - b) < 1e-9;
	}

	// Runs `frames` frames of `frameSeconds` each and returns the steps they asked for in all.
	uint64_t RunFrames(FixedStepClock& clock, int frames, double frameSeconds)
	{
		uint64_t steps = 0;
		for (int frame = 0; frame < frames; ++frame)
		{
			steps += clock.Advance(frameSeconds);
		}
		return steps;
	}

	// A second of frames gives 60 steps, whatever the frame rate.
	void TestOneSecondIsSixtyStepsAtAnyFrameRate()
	{
		for (const int framesPerSecond : { 30, 60, 144 })
		{
			FixedStepClock clock;
			const uint64_t steps = RunFrames(clock, framesPerSecond, 1.0 / framesPerSecond);
			Require(steps == 60, "A second of frames at " + std::to_string(framesPerSecond) + " fps should give 60 steps, gave " + std::to_string(steps));
			Require(!clock.IsDroppingTime() && clock.GetDroppedSeconds() == 0.0, "Frames at " + std::to_string(framesPerSecond) + " fps should drop no time");
		}
	}

	// A slow frame runs at most four steps and drops the rest; real time beyond the clamp is
	// dropped too. The clock says so until a frame drops nothing.
	void TestStepsPerFrameAreCapped()
	{
		FixedStepClock clock;
		Require(clock.Advance(6.5 * Step) == FixedStepClock::MaxStepsPerFrame, "A frame of six and a half steps should run four");
		Require(clock.IsDroppingTime(), "A frame over the cap should be dropping time");
		Require(Near(clock.GetDroppedSeconds(), 2.0 * Step), "A frame of six and a half steps should drop two");
		Require(Near(clock.GetInterpolationFactor(), 0.5), "The half step left over should wait for the next frame");

		Require(clock.Advance(Step) == 1, "The half step and a step should run one");
		Require(!clock.IsDroppingTime(), "A frame under the cap should not be dropping time");

		FixedStepClock hitched;
		Require(hitched.Advance(10.0) == FixedStepClock::MaxStepsPerFrame, "A ten second hitch should run four steps");
		Require(Near(hitched.GetDroppedSeconds(), 10.0 - 4.0 * Step), "A ten second hitch should drop all but four steps");
		Require(hitched.GetWorldTimeSeconds() < 0.1, "A hitch should not move the world on by more than four steps");
	}

	// The interpolation factor is how far into the next step the frame is, never 1 or more.
	void TestInterpolationFactorStaysInRange()
	{
		FixedStepClock clock;
		Require(clock.GetInterpolationFactor() == 0.0, "A new clock should be at the start of a step");
		clock.Advance(0.25 * Step);
		Require(Near(clock.GetInterpolationFactor(), 0.25), "A quarter step in should give a quarter");
		for (int frame = 0; frame < 1000; ++frame)
		{
			clock.Advance(0.37 * Step + frame * 1e-5);
			const double factor = clock.GetInterpolationFactor();
			Require(factor >= 0.0 && factor < 1.0, "The interpolation factor left [0, 1): " + std::to_string(factor));
		}
	}

	// While paused, real time doesn't count; StepOnce runs exactly one step on the next frame.
	void TestPauseAndStepOnce()
	{
		FixedStepClock clock;
		clock.Advance(0.5 * Step);
		clock.SetPaused(true);
		Require(clock.IsPaused(), "The clock should be paused");
		Require(RunFrames(clock, 100, 1.0 / 60.0) == 0, "A paused clock should run no steps");
		Require(!clock.IsDroppingTime() && clock.GetDroppedSeconds() == 0.0, "Paused time should not count as dropped");

		clock.StepOnce();
		clock.StepOnce();
		Require(clock.Advance(1.0 / 60.0) == 1, "StepOnce while paused should run exactly one step");
		Require(clock.Advance(1.0 / 60.0) == 0, "After its step, the clock should stay paused");
		Require(clock.Advance(0.0) == 0, "StepOnce should run one step, once");

		clock.SetPaused(false);
		clock.StepOnce();
		Require(clock.Advance(0.0) == 0, "StepOnce while running should do nothing");
		Require(clock.Advance(0.5 * Step) == 1, "Unpaused, the half step from before the pause should still count");
	}

	// The time scale multiplies the real time added, within its range.
	void TestTimeScale()
	{
		FixedStepClock slow;
		slow.SetTimeScale(0.5);
		Require(RunFrames(slow, 60, 1.0 / 60.0) == 30, "A second at half speed should give 30 steps");

		FixedStepClock fast;
		fast.SetTimeScale(2.0);
		Require(RunFrames(fast, 60, 1.0 / 60.0) == 120, "A second at double speed should give 120 steps");

		FixedStepClock clamped;
		clamped.SetTimeScale(0.0);
		Require(clamped.GetTimeScale() == FixedStepClock::MinTimeScale, "A time scale of 0 should be clamped to the minimum");
		clamped.SetTimeScale(100.0);
		Require(clamped.GetTimeScale() == FixedStepClock::MaxTimeScale, "A time scale of 100 should be clamped to the maximum");
	}

	// Steps count up from 0, and world time is steps times the step length.
	void TestStepIndexAndWorldTime()
	{
		FixedStepClock clock;
		Require(clock.Advance(3.0 * Step) == 3, "Three steps of time should give three steps");
		for (uint32_t step = 0; step < 3; ++step)
		{
			const StepTime stepTime = clock.GetFrameStep(step);
			Require(stepTime.StepIndex == step, "The frame's steps should count up from 0");
			Require(Near(stepTime.WorldTimeSeconds, step * Step), "A step should start at its index times the step length");
			Require(stepTime.FixedDeltaSeconds == Step, "A step should be one step long");
		}
		Require(clock.GetStepIndex() == 3, "Three steps should have run");
		Require(Near(clock.GetWorldTimeSeconds(), 3.0 * Step), "World time should be three steps");

		clock.Advance(Step);
		Require(clock.GetFrameStep(0).StepIndex == 3, "The next frame's step should go on from the last");

		// A new step length counts from where the world is; the time already passed stays.
		clock.SetStepSeconds(0.1);
		Require(clock.GetStepSeconds() == 0.1, "The step length was lost");
		clock.SetStepSeconds(0.0);
		Require(clock.GetStepSeconds() == 0.1, "A step length of 0 should be ignored");
		Require(clock.Advance(0.2) == 2, "Two steps of the new length should give two steps");
		Require(clock.GetStepIndex() == 6, "Steps should go on counting up");
		Require(Near(clock.GetWorldTimeSeconds(), 4.0 * Step + 0.2), "World time should go on from where it was");
	}
}

int main()
{
	try
	{
		TestOneSecondIsSixtyStepsAtAnyFrameRate();
		TestStepsPerFrameAreCapped();
		TestInterpolationFactorStaysInRange();
		TestPauseAndStepOnce();
		TestTimeScale();
		TestStepIndexAndWorldTime();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Fixed step clock test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All fixed step clock tests passed.\n";
	return 0;
}

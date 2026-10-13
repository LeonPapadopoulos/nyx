// The frame rate cap (FrameRateLimiter): when each frame is due, and that waiting holds a loop to
// its pace on the real clock.
#include "FrameRateLimiter.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using Nyx::FrameRateLimiter;
	using Clock = FrameRateLimiter::Clock;
	using namespace std::chrono_literals;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	// A frame on time, or a little late, keeps the pace: the next is due one interval after this one was.
	void TestFramesKeepTheirPace()
	{
		const Clock::time_point due = Clock::time_point{} + 1s;
		const Clock::duration interval = 10ms;
		Require(FrameRateLimiter::NextFrameDue(due, due, interval) == due + interval, "A frame on time should make the next due one interval later");
		Require(FrameRateLimiter::NextFrameDue(due, due + 4ms, interval) == due + interval,
			"A frame that started a little late should keep the pace, not push the next one back");
	}

	// A loop that fell behind by more than an interval starts its next frame at once, and doesn't
	// rush through frames to catch up.
	void TestALoopBehindStartsAtOnce()
	{
		const Clock::time_point due = Clock::time_point{} + 1s;
		const Clock::time_point now = due + 35ms;
		Require(FrameRateLimiter::NextFrameDue(due, now, 10ms) == now, "A loop behind by more than an interval should start its next frame now");
	}

	// Without a cap, nothing waits; with one, frames take at least their share on the real clock.
	void TestWaitingHoldsThePace()
	{
		FrameRateLimiter limiter;
		Require(limiter.GetFramesPerSecond() == 0, "A limiter should start without a cap");
		const Clock::time_point uncappedStart = Clock::now();
		for (int frame = 0; frame < 100; ++frame)
		{
			limiter.WaitForNextFrame();
		}
		Require(Clock::now() - uncappedStart < 50ms, "Without a cap, waiting for the next frame should return at once");

		// The first frame starts at once; the next four wait 10 ms each.
		limiter.SetFramesPerSecond(100);
		Require(limiter.GetFramesPerSecond() == 100, "The cap was lost");
		const Clock::time_point cappedStart = Clock::now();
		for (int frame = 0; frame < 5; ++frame)
		{
			limiter.WaitForNextFrame();
		}
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - cappedStart);
		Require(elapsed >= 39ms, "Five frames at 100 fps should take at least 40 ms, took " + std::to_string(elapsed.count()) + " us");
	}
}

int main()
{
	try
	{
		TestFramesKeepTheirPace();
		TestALoopBehindStartsAtOnce();
		TestWaitingHoldsThePace();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Frame rate limiter test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All frame rate limiter tests passed.\n";
	return 0;
}

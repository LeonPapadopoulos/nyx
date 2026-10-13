#pragma once

#include "NyxEngineAPI.h"

#include <chrono>
#include <cstdint>

namespace Nyx
{
	// Holds a frame loop to at most a number of frames per second, by waiting before each frame
	// until its time has come. Without a limit, it never waits.
	class NYXENGINE_API FrameRateLimiter
	{
	public:
		using Clock = std::chrono::steady_clock;

		FrameRateLimiter();
		~FrameRateLimiter();
		FrameRateLimiter(const FrameRateLimiter&) = delete;
		FrameRateLimiter& operator=(const FrameRateLimiter&) = delete;

		// 0 for no limit
		void SetFramesPerSecond(uint32_t framesPerSecond);
		uint32_t GetFramesPerSecond() const { return FramesPerSecond; }

		// Returns once the next frame may start: at once without a limit, or when the last frame
		// took longer than its share.
		void WaitForNextFrame();

		// When the frame after one that was due at `due` is due, seen at `now`: one interval
		// later, so the frames keep their pace when one starts a little late. A loop that fell
		// behind by more than an interval starts its next frame now instead of rushing to catch up.
		static Clock::time_point NextFrameDue(Clock::time_point due, Clock::time_point now, Clock::duration interval);

	private:
		void WaitUntil(Clock::time_point time);

		uint32_t FramesPerSecond = 0;

		// When the next frame is due; unset until the first frame under a limit
		Clock::time_point Due{};

		// On Windows, a high resolution waitable timer: a plain sleep wakes up as much as 15 ms
		// late, more than a frame at 60 fps.
		void* Timer = nullptr;
	};
}

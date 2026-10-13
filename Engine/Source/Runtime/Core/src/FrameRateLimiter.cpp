#include "FrameRateLimiter.h"

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
	// Windows 10 1803 and later; older SDKs don't name it
	#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
		#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
	#endif
#endif

#include <thread>

namespace Nyx
{
	FrameRateLimiter::FrameRateLimiter()
	{
#if defined(_WIN32)
		// Without the high resolution flag (older Windows), a plain waitable timer
		Timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (Timer == nullptr)
		{
			Timer = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		}
#endif
	}

	FrameRateLimiter::~FrameRateLimiter()
	{
#if defined(_WIN32)
		if (Timer != nullptr)
		{
			::CloseHandle(Timer);
		}
#endif
	}

	void FrameRateLimiter::SetFramesPerSecond(uint32_t framesPerSecond)
	{
		if (framesPerSecond != FramesPerSecond)
		{
			FramesPerSecond = framesPerSecond;
			// The new pace starts with the next frame.
			Due = Clock::time_point{};
		}
	}

	void FrameRateLimiter::WaitForNextFrame()
	{
		if (FramesPerSecond == 0)
		{
			return;
		}

		const Clock::duration interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / FramesPerSecond));
		if (Due == Clock::time_point{})
		{
			Due = Clock::now();
		}
		WaitUntil(Due);
		Due = NextFrameDue(Due, Clock::now(), interval);
	}

	FrameRateLimiter::Clock::time_point FrameRateLimiter::NextFrameDue(Clock::time_point due, Clock::time_point now, Clock::duration interval)
	{
		const Clock::time_point next = due + interval;
		return next < now ? now : next;
	}

	void FrameRateLimiter::WaitUntil(Clock::time_point time)
	{
		const Clock::duration remaining = time - Clock::now();
		if (remaining <= Clock::duration::zero())
		{
			return;
		}

#if defined(_WIN32)
		if (Timer != nullptr)
		{
			// Negative: relative to now, in 100 ns units
			LARGE_INTEGER dueTime{};
			dueTime.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
			if (::SetWaitableTimer(Timer, &dueTime, 0, nullptr, nullptr, FALSE))
			{
				::WaitForSingleObject(Timer, INFINITE);
				return;
			}
		}
#endif
		std::this_thread::sleep_until(time);
	}
}

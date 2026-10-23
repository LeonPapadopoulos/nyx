#pragma once

#include <chrono>
#include <cstdint>

namespace Nyx
{
	// Milliseconds since 1970 (UTC): when something happened, e.g. a log line or the start of a
	// recording
	inline uint64_t GetClockTimeMs()
	{
		const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
	}
}

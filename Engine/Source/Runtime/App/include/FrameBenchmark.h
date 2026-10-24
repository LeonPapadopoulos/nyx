#pragma once

#include "FrameStats.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	// Where a benchmark's camera is at a moment of its recording: a position and a point it looks at
	struct BenchmarkCameraKey
	{
		float Seconds = 0.0f;
		glm::vec3 Position{ 0.0f };
		glm::vec3 LookAt{ 0.0f, 0.0f, -1.0f };
	};

	// A camera's path through a benchmark, so every run sees the same: keys in time order, the camera
	// moving in a straight line from one to the next.
	struct BenchmarkCameraPath
	{
		std::vector<BenchmarkCameraKey> Keys;
	};

	// Reads a camera path, a key a line: "<seconds> <x> <y> <z> <look-at x> <look-at y> <look-at z>",
	// the seconds since the recording began, in increasing order; blank lines and lines from "#" on
	// are skipped. Empty, with `outError` saying why, when a line is malformed or there is no key.
	std::optional<BenchmarkCameraPath> ParseBenchmarkCameraPath(std::string_view text, std::string& outError);
	std::optional<BenchmarkCameraPath> LoadBenchmarkCameraPath(const std::filesystem::path& path, std::string& outError);

	// Where the path has the camera `seconds` into the recording: at the first key before it, at the
	// last after it, and in between on the line from one key to the next
	BenchmarkCameraKey SampleBenchmarkCameraPath(const BenchmarkCameraPath& path, float seconds);

	// A benchmark's frames summed up: each number's average, its 1% low (the value only the slowest
	// 1% of the frames exceed, its 99th percentile: every number is a time or a count, so higher is
	// slower) and its worst (the highest)
	struct FrameBenchmarkSummary
	{
		uint32_t Frames = 0;
		double Milliseconds = 0.0;
		FrameStatsSample Average;
		FrameStatsSample OnePercentLow;
		FrameStatsSample Worst;
	};
	FrameBenchmarkSummary SummarizeFrameBenchmark(std::span<const FrameStatsSample> frames);

	// The benchmark as CSV: a header of the columns ("frame", "time_s", then each EFrameStat's
	// name), a row a frame (its number from 0, the seconds from the recording's start to the frame's
	// end, its numbers), then the summary, a row each with "average", "1% low" and "worst" as its
	// frame and its frames' seconds in all as its time
	std::string MakeFrameBenchmarkCsv(std::span<const FrameStatsSample> frames, const FrameBenchmarkSummary& summary);

	// The summary in a line, for the log, e.g. "600 frames in 10.0 s: average 16.7 ms (60 fps), 1% low
	// 18.1 ms (55 fps), worst 20.4 ms (49 fps)"
	std::string FormatFrameBenchmarkSummary(const FrameBenchmarkSummary& summary);
}

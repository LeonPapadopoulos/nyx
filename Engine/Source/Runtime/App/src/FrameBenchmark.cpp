#include "FrameBenchmark.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
	// The next number of the line from `at` on, after spaces and tabs
	std::optional<float> TakeNumber(std::string_view line, size_t& at)
	{
		while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
		{
			++at;
		}
		size_t end = at;
		while (end < line.size() && line[end] != ' ' && line[end] != '\t')
		{
			++end;
		}
		float number = 0.0f;
		const auto [last, error] = std::from_chars(line.data() + at, line.data() + end, number);
		if (end == at || error != std::errc() || last != line.data() + end || !std::isfinite(number))
		{
			return std::nullopt;
		}
		at = end;
		return number;
	}

	// A number for the CSV: a time with three decimals, a count whole
	void AppendNumber(std::string& text, double value, bool bMilliseconds)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), bMilliseconds ? "%.3f" : "%.0f", value);
		text += buffer;
	}

	void AppendRow(std::string& text, std::string_view frame, double seconds, const Nyx::FrameStatsSample& sample)
	{
		text += frame;
		text += ',';
		AppendNumber(text, seconds, true);
		for (size_t stat = 0; stat < Nyx::FrameStatCount; ++stat)
		{
			text += ',';
			AppendNumber(text, sample.Values[stat], Nyx::GetFrameStatInfo(static_cast<Nyx::EFrameStat>(stat)).bMilliseconds);
		}
		text += '\n';
	}

	double FramesPerSecond(double milliseconds)
	{
		return milliseconds > 0.0 ? 1000.0 / milliseconds : 0.0;
	}
}

namespace Nyx
{
	std::optional<BenchmarkCameraPath> ParseBenchmarkCameraPath(std::string_view text, std::string& outError)
	{
		BenchmarkCameraPath path;
		uint32_t lineNumber = 0;
		while (!text.empty())
		{
			const size_t lineEnd = text.find('\n');
			std::string_view line = text.substr(0, lineEnd);
			text = lineEnd == std::string_view::npos ? std::string_view() : text.substr(lineEnd + 1);
			++lineNumber;

			line = line.substr(0, line.find('#'));
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			{
				line.remove_suffix(1);
			}
			if (line.find_first_not_of(" \t") == std::string_view::npos)
			{
				continue;
			}

			float numbers[7] = {};
			size_t at = 0;
			for (float& number : numbers)
			{
				const std::optional<float> value = TakeNumber(line, at);
				if (!value)
				{
					outError = "Line " + std::to_string(lineNumber) + " of the camera path needs <seconds> <x> <y> <z> <look-at x> <look-at y> <look-at z>";
					return std::nullopt;
				}
				number = *value;
			}
			if (line.find_first_not_of(" \t", at) != std::string_view::npos)
			{
				outError = "Line " + std::to_string(lineNumber) + " of the camera path has more than seven numbers";
				return std::nullopt;
			}

			const BenchmarkCameraKey key{ .Seconds = numbers[0], .Position = glm::vec3(numbers[1], numbers[2], numbers[3]), .LookAt = glm::vec3(numbers[4], numbers[5], numbers[6]) };
			if (!path.Keys.empty() && key.Seconds < path.Keys.back().Seconds)
			{
				outError = "Line " + std::to_string(lineNumber) + " of the camera path goes back in time: its keys must be in time order";
				return std::nullopt;
			}
			if (key.Position == key.LookAt)
			{
				outError = "Line " + std::to_string(lineNumber) + " of the camera path looks at where the camera is";
				return std::nullopt;
			}
			path.Keys.push_back(key);
		}

		if (path.Keys.empty())
		{
			outError = "The camera path has no keys";
			return std::nullopt;
		}
		return path;
	}

	std::optional<BenchmarkCameraPath> LoadBenchmarkCameraPath(const std::filesystem::path& path, std::string& outError)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			outError = "Can't read the camera path '" + path.string() + "'";
			return std::nullopt;
		}
		std::ostringstream text;
		text << file.rdbuf();
		return ParseBenchmarkCameraPath(text.str(), outError);
	}

	BenchmarkCameraKey SampleBenchmarkCameraPath(const BenchmarkCameraPath& path, float seconds)
	{
		if (path.Keys.empty())
		{
			return BenchmarkCameraKey{};
		}
		if (seconds <= path.Keys.front().Seconds)
		{
			return path.Keys.front();
		}
		for (size_t index = 1; index < path.Keys.size(); ++index)
		{
			const BenchmarkCameraKey& from = path.Keys[index - 1];
			const BenchmarkCameraKey& to = path.Keys[index];
			if (seconds < to.Seconds)
			{
				const float t = (seconds - from.Seconds) / (to.Seconds - from.Seconds);
				return BenchmarkCameraKey{ .Seconds = seconds, .Position = glm::mix(from.Position, to.Position, t), .LookAt = glm::mix(from.LookAt, to.LookAt, t) };
			}
		}
		return path.Keys.back();
	}

	FrameBenchmarkSummary SummarizeFrameBenchmark(std::span<const FrameStatsSample> frames)
	{
		FrameBenchmarkSummary summary;
		summary.Frames = static_cast<uint32_t>(frames.size());
		if (frames.empty())
		{
			return summary;
		}

		std::vector<double> values(frames.size());
		// The nearest rank: the value at or above which the slowest 1% of the frames are
		const size_t onePercentLowRank = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(frames.size()))) - 1;
		for (size_t stat = 0; stat < FrameStatCount; ++stat)
		{
			double sum = 0.0;
			for (size_t frame = 0; frame < frames.size(); ++frame)
			{
				values[frame] = frames[frame].Values[stat];
				sum += values[frame];
			}
			summary.Average.Values[stat] = sum / static_cast<double>(frames.size());
			std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(onePercentLowRank), values.end());
			summary.OnePercentLow.Values[stat] = values[onePercentLowRank];
			summary.Worst.Values[stat] = *std::max_element(values.begin(), values.end());
		}
		summary.Milliseconds = summary.Average[EFrameStat::FrameTime] * static_cast<double>(frames.size());
		return summary;
	}

	std::string MakeFrameBenchmarkCsv(std::span<const FrameStatsSample> frames, const FrameBenchmarkSummary& summary)
	{
		std::string text = "frame,time_s";
		for (size_t stat = 0; stat < FrameStatCount; ++stat)
		{
			text += ',';
			text += GetFrameStatInfo(static_cast<EFrameStat>(stat)).Name;
		}
		text += '\n';

		double milliseconds = 0.0;
		for (size_t frame = 0; frame < frames.size(); ++frame)
		{
			milliseconds += frames[frame][EFrameStat::FrameTime];
			AppendRow(text, std::to_string(frame), milliseconds / 1000.0, frames[frame]);
		}

		AppendRow(text, "average", summary.Milliseconds / 1000.0, summary.Average);
		AppendRow(text, "1% low", summary.Milliseconds / 1000.0, summary.OnePercentLow);
		AppendRow(text, "worst", summary.Milliseconds / 1000.0, summary.Worst);
		return text;
	}

	std::string FormatFrameBenchmarkSummary(const FrameBenchmarkSummary& summary)
	{
		const double average = summary.Average[EFrameStat::FrameTime];
		const double onePercentLow = summary.OnePercentLow[EFrameStat::FrameTime];
		const double worst = summary.Worst[EFrameStat::FrameTime];
		char text[256];
		std::snprintf(text, sizeof(text), "%u frames in %.1f s: average %.2f ms (%.0f fps), 1%% low %.2f ms (%.0f fps), worst %.2f ms (%.0f fps); GPU average %.2f ms",
			summary.Frames, summary.Milliseconds / 1000.0, average, FramesPerSecond(average), onePercentLow, FramesPerSecond(onePercentLow), worst,
			FramesPerSecond(worst), summary.Average[EFrameStat::GpuFrame]);
		return text;
	}
}

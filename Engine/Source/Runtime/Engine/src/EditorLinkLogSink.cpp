#include "NyxPCH.h"
#include "EditorLinkLogSink.h"

#include "Log.h"

#include <algorithm>
#include <chrono>

namespace Nyx::Engine
{
	std::shared_ptr<EditorLinkLogSink> EditorLinkLogSink::Install()
	{
		auto sink = std::make_shared<EditorLinkLogSink>();

		Nyx::Core::Logger& logger = Nyx::Core::Logger::Get();
		for (const std::shared_ptr<spdlog::logger>& target : { logger.GetCoreLogger(), logger.GetClientLogger() })
		{
			if (target)
			{
				target->sinks().push_back(sink);
			}
		}

		return sink;
	}

	std::vector<LogLineMessage> EditorLinkLogSink::TakeLines()
	{
		std::lock_guard lock(Mutex);

		std::vector<LogLineMessage> lines;
		lines.swap(Lines);

		if (DroppedLineCount > 0)
		{
			LogLineMessage dropped;
			dropped.Level = ELogLevel::Warning;
			dropped.TimeMs = GetClockTimeMs();
			dropped.LoggerName = "LINK";
			dropped.Text = std::to_string(DroppedLineCount) + " log lines were dropped here, because they weren't sent in time";
			lines.push_back(std::move(dropped));
			DroppedLineCount = 0;
		}

		return lines;
	}

	void EditorLinkLogSink::Stop()
	{
		std::lock_guard lock(Mutex);
		bStopped = true;
		Lines.clear();
		DroppedLineCount = 0;
	}

	void EditorLinkLogSink::log(const spdlog::details::log_msg& message)
	{
		LogLineMessage line;
		line.Level = static_cast<ELogLevel>(std::min<int>(static_cast<int>(message.level), static_cast<int>(ELogLevel::Critical)));
		line.TimeMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(message.time.time_since_epoch()).count());
		line.LoggerName.assign(message.logger_name.data(), message.logger_name.size());
		line.Text.assign(message.payload.data(), message.payload.size());

		std::lock_guard lock(Mutex);
		if (bStopped)
		{
			return;
		}

		if (Lines.size() >= MaxKeptLines)
		{
			++DroppedLineCount;
			return;
		}

		Lines.push_back(std::move(line));
	}
}

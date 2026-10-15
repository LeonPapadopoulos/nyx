#pragma once

#include "EditorLinkMessages.h"

#include <spdlog/sinks/sink.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace Nyx::Engine
{
	// Collects the program's log lines, from any thread, to send them over the editor link as
	// LogLine messages. Install it before anything worth sending is logged; lines are kept until
	// the link takes them, so lines from before the link exists aren't lost.
	class EditorLinkLogSink : public spdlog::sinks::sink
	{
	public:
		// Adds a new sink to the engine's and the application's logger, after Logger::Init().
		// Call it at startup, before other threads log: adding sinks isn't thread-safe.
		static std::shared_ptr<EditorLinkLogSink> Install();

		// The lines logged since the last call, oldest first. If lines had to be dropped because
		// nobody took them, the last line says how many.
		std::vector<LogLineMessage> TakeLines();

		// Stops collecting, e.g. once the link is gone for good; lines logged afterwards are dropped
		void Stop();

		// spdlog's sink interface. Lines are sent without the console's pattern: the receiver
		// shows level, logger and time itself.
		void log(const spdlog::details::log_msg& message) override;
		void flush() override
		{
		}
		void set_pattern(const std::string& /*pattern*/) override
		{
		}
		void set_formatter(std::unique_ptr<spdlog::formatter> /*formatter*/) override
		{
		}

		// More lines than this aren't kept while nobody takes them
		static constexpr size_t MaxKeptLines = 10000;

	private:
		std::mutex Mutex;
		std::vector<LogLineMessage> Lines;
		size_t DroppedLineCount = 0;
		bool bStopped = false;
	};
}

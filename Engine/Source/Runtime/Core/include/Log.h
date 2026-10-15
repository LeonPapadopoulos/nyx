#pragma once
#include "NyxEngineAPI.h"
#include "spdlog/spdlog.h"

#include <cstdint>
#include <memory>

namespace Nyx
{
	class LogCountSink;

	// How many warnings and errors were logged, e.g. for a status bar
	struct LogCounts
	{
		uint64_t Warnings = 0;
		// Errors and critical errors
		uint64_t Errors = 0;
	};

	class NYXENGINE_API Logger
	{
	public:
		static Logger& Get();
		void Init();

	public:
		Logger() = default;

		Logger(const Logger&) = delete;
		Logger(Logger&&) = delete;

		Logger& operator=(const Logger&) = delete;
		Logger& operator=(Logger&&) = delete;

		std::shared_ptr<spdlog::logger>& GetCoreLogger();
		std::shared_ptr<spdlog::logger>& GetClientLogger();

		// Warnings and errors both loggers logged since Init(), from any thread
		LogCounts GetCounts() const;

	private:
		std::shared_ptr<spdlog::logger> CoreLogger;
		std::shared_ptr<spdlog::logger> ClientLogger;
		std::shared_ptr<LogCountSink> Counts;
	};
}

// Core Log Macros
#define CORE_LOG_TRACE(...)    ::Nyx::Logger::Get().GetCoreLogger()->trace(__VA_ARGS__)
#define CORE_LOG_INFO(...)     ::Nyx::Logger::Get().GetCoreLogger()->info(__VA_ARGS__)
#define CORE_LOG_WARNING(...)  ::Nyx::Logger::Get().GetCoreLogger()->warn(__VA_ARGS__)
#define CORE_LOG_ERROR(...)    ::Nyx::Logger::Get().GetCoreLogger()->error(__VA_ARGS__)
#define CORE_LOG_CRITICAL(...) ::Nyx::Logger::Get().GetCoreLogger()->critical(__VA_ARGS__)

// Client Log Macros
#define LOG_TRACE(...)         ::Nyx::Logger::Get().GetClientLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)          ::Nyx::Logger::Get().GetClientLogger()->info(__VA_ARGS__)
#define LOG_WARNING(...)       ::Nyx::Logger::Get().GetClientLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)         ::Nyx::Logger::Get().GetClientLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...)      ::Nyx::Logger::Get().GetClientLogger()->critical(__VA_ARGS__)
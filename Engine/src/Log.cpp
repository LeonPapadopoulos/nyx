#include "Enginepch.h"
#include "Log.h"
#include "Core.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Engine
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		// Timestamp, LoggerName, LogMessage
		spdlog::set_pattern("%^[%T] %n: %v%$");
		
		// @todo: Expose Core log level
		s_CoreLogger = spdlog::stdout_color_mt("ENGINE");
		s_CoreLogger->set_level(spdlog::level::level_enum::trace);
		// @todo: Expose Client log level
		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_CoreLogger->set_level(spdlog::level::level_enum::trace);
	}
}


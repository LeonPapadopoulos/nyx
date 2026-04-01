#include "NyxPCH.h"
#include "Log.h"
#include "Core.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Nyx
{
	namespace Core
	{
		Logger& Logger::Get()
		{
			static Logger Instance{};
			return Instance;
		}

		void Logger::Init()
		{
			spdlog::set_pattern("%^[%T] %n: %v%$");

			CoreLogger = spdlog::stdout_color_mt("ENGINE");
			CoreLogger->set_level(spdlog::level::trace);

			ClientLogger = spdlog::stdout_color_mt("APP");
			ClientLogger->set_level(spdlog::level::trace);
		}

		std::shared_ptr<spdlog::logger>& Logger::GetCoreLogger()
		{
			return CoreLogger;
		}

		std::shared_ptr<spdlog::logger>& Logger::GetClientLogger()
		{
			return ClientLogger;
		}
	}
}


#pragma once
#include "NyxEngineAPI.h"
#include "spdlog/spdlog.h"

#include <memory>

namespace Nyx
{
	namespace Core
	{
		class NYXENGINE_API Log
		{
		public:
			static void Init();

			static std::shared_ptr<spdlog::logger>& GetCoreLogger();
			static std::shared_ptr<spdlog::logger>& GetClientLogger();

		private:
			static std::shared_ptr<spdlog::logger> s_CoreLogger;
			static std::shared_ptr<spdlog::logger> s_ClientLogger;
		};
	}
}

// Core Log Macros
#define CORE_LOG_TRACE(...)    ::Nyx::Core::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define CORE_LOG_INFO(...)     ::Nyx::Core::Log::GetCoreLogger()->info(__VA_ARGS__)
#define CORE_LOG_WARNING(...)  ::Nyx::Core::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define CORE_LOG_ERROR(...)    ::Nyx::Core::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CORE_LOG_CRITICAL(...) ::Nyx::Core::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client Log Macros
#define LOG_TRACE(...)         ::Nyx::Core::Log::GetClientLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)          ::Nyx::Core::Log::GetClientLogger()->info(__VA_ARGS__)
#define LOG_WARNING(...)       ::Nyx::Core::Log::GetClientLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)         ::Nyx::Core::Log::GetClientLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...)      ::Nyx::Core::Log::GetClientLogger()->critical(__VA_ARGS__)
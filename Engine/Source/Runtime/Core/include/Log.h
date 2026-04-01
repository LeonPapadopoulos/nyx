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
			static Log& Get();
			void Init();

		public:
			Log() = default;

			Log(const Log&) = delete;
			Log(Log&&) = delete;

			Log& operator=(const Log&) = delete;
			Log& operator=(Log&&) = delete;

			std::shared_ptr<spdlog::logger>& GetCoreLogger();
			std::shared_ptr<spdlog::logger>& GetClientLogger();

		private:
			std::shared_ptr<spdlog::logger> CoreLogger;
			std::shared_ptr<spdlog::logger> ClientLogger;
		};
	}
}

// Core Log Macros
#define CORE_LOG_TRACE(...)    ::Nyx::Core::Log::Get().GetCoreLogger()->trace(__VA_ARGS__)
#define CORE_LOG_INFO(...)     ::Nyx::Core::Log::Get().GetCoreLogger()->info(__VA_ARGS__)
#define CORE_LOG_WARNING(...)  ::Nyx::Core::Log::Get().GetCoreLogger()->warn(__VA_ARGS__)
#define CORE_LOG_ERROR(...)    ::Nyx::Core::Log::Get().GetCoreLogger()->error(__VA_ARGS__)
#define CORE_LOG_CRITICAL(...) ::Nyx::Core::Log::Get().GetCoreLogger()->critical(__VA_ARGS__)

// Client Log Macros
#define LOG_TRACE(...)         ::Nyx::Core::Log::Get().GetClientLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)          ::Nyx::Core::Log::Get().GetClientLogger()->info(__VA_ARGS__)
#define LOG_WARNING(...)       ::Nyx::Core::Log::Get().GetClientLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)         ::Nyx::Core::Log::Get().GetClientLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...)      ::Nyx::Core::Log::Get().GetClientLogger()->critical(__VA_ARGS__)
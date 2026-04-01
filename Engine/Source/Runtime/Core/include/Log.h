#pragma once
#include "NyxEngineAPI.h"
#include "spdlog/spdlog.h"

#include <memory>

namespace Nyx
{
	namespace Core
	{
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

		private:
			std::shared_ptr<spdlog::logger> CoreLogger;
			std::shared_ptr<spdlog::logger> ClientLogger;
		};
	}
}

// Core Log Macros
#define CORE_LOG_TRACE(...)    ::Nyx::Core::Logger::Get().GetCoreLogger()->trace(__VA_ARGS__)
#define CORE_LOG_INFO(...)     ::Nyx::Core::Logger::Get().GetCoreLogger()->info(__VA_ARGS__)
#define CORE_LOG_WARNING(...)  ::Nyx::Core::Logger::Get().GetCoreLogger()->warn(__VA_ARGS__)
#define CORE_LOG_ERROR(...)    ::Nyx::Core::Logger::Get().GetCoreLogger()->error(__VA_ARGS__)
#define CORE_LOG_CRITICAL(...) ::Nyx::Core::Logger::Get().GetCoreLogger()->critical(__VA_ARGS__)

// Client Log Macros
#define LOG_TRACE(...)         ::Nyx::Core::Logger::Get().GetClientLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)          ::Nyx::Core::Logger::Get().GetClientLogger()->info(__VA_ARGS__)
#define LOG_WARNING(...)       ::Nyx::Core::Logger::Get().GetClientLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)         ::Nyx::Core::Logger::Get().GetClientLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...)      ::Nyx::Core::Logger::Get().GetClientLogger()->critical(__VA_ARGS__)
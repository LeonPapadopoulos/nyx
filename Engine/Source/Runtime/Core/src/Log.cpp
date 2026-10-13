#include "NyxPCH.h"
#include "Log.h"
#include "Core.h"
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>

namespace Nyx
{
	namespace Core
	{
		// Counts the warnings and errors it is given, from any thread; prints nothing
		class LogCountSink final : public spdlog::sinks::sink
		{
		public:
			void log(const spdlog::details::log_msg& message) override
			{
				if (message.level == spdlog::level::warn)
				{
					Warnings.fetch_add(1, std::memory_order_relaxed);
				}
				else if (message.level == spdlog::level::err || message.level == spdlog::level::critical)
				{
					Errors.fetch_add(1, std::memory_order_relaxed);
				}
			}
			void flush() override
			{
			}
			void set_pattern(const std::string& /*pattern*/) override
			{
			}
			void set_formatter(std::unique_ptr<spdlog::formatter> /*formatter*/) override
			{
			}

			LogCounts Get() const
			{
				return LogCounts{ Warnings.load(std::memory_order_relaxed), Errors.load(std::memory_order_relaxed) };
			}

		private:
			std::atomic<uint64_t> Warnings = 0;
			std::atomic<uint64_t> Errors = 0;
		};

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

			Counts = std::make_shared<LogCountSink>();
			CoreLogger->sinks().push_back(Counts);
			ClientLogger->sinks().push_back(Counts);
		}

		std::shared_ptr<spdlog::logger>& Logger::GetCoreLogger()
		{
			return CoreLogger;
		}

		std::shared_ptr<spdlog::logger>& Logger::GetClientLogger()
		{
			return ClientLogger;
		}

		LogCounts Logger::GetCounts() const
		{
			return Counts ? Counts->Get() : LogCounts{};
		}
	}
}

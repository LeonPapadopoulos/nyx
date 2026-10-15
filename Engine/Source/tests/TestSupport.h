#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

// What every test program shares. A test fails by throwing; the program's main() reports it.
namespace Nyx::Tests
{
	inline void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	// A folder of the test's own in the temp folder, removed with everything in it at the end.
	// It is always new: a folder that exists already is never used, so a test never removes
	// anything it didn't make.
	class TestDirectory
	{
	public:
		explicit TestDirectory(std::string_view name)
		{
			const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (int attempt = 0; attempt < 64; ++attempt)
			{
				const std::filesystem::path candidate = std::filesystem::temp_directory_path() /
					(std::string(name) + "-" + std::to_string(timestamp) + "-" + std::to_string(attempt));
				if (std::filesystem::create_directory(candidate))
				{
					Path = candidate;
					return;
				}
			}
			throw std::runtime_error("Could not create the test directory");
		}

		~TestDirectory()
		{
			std::error_code ignored;
			std::filesystem::remove_all(Path, ignored);
		}

		TestDirectory(const TestDirectory&) = delete;
		TestDirectory& operator=(const TestDirectory&) = delete;

		std::filesystem::path Path;
	};
}

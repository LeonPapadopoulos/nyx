#include "WindowsStartupBanner.h"
#include "Paths.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
	// The mode's name as written in Startup.ini.
	const char* GetModeName(Nyx::EStartupBannerMode mode)
	{
		switch (mode)
		{
		case Nyx::EStartupBannerMode::Lightning:  return "lightning";
		case Nyx::EStartupBannerMode::RealityCut: return "reality-cut";
		case Nyx::EStartupBannerMode::Chasm:      return "chasm";
		case Nyx::EStartupBannerMode::Rift:       return "rift";
		case Nyx::EStartupBannerMode::Assemble:   return "assemble";
		case Nyx::EStartupBannerMode::AssembleV2: return "assemble_v2";
		default:                                  return "classic";
		}
	}
}

// Explicit mode flags override Startup.ini for this launch only.
int main(int argc, char** argv)
{
	try
	{
		Nyx::EStartupBannerMode mode = Nyx::EStartupBannerMode::Configured;
		std::filesystem::path artworkDirectory;
		std::filesystem::path exportDirectory;
		bool bPrintArtworkPath = false;
		bool bPrintMode = false;
		bool bBenchmark = false;

		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument(argv[index]);
			if (argument == "--classic")
			{
				mode = Nyx::EStartupBannerMode::Classic;
			}
			else if (argument == "--cinematic" || argument == "--lightning")
			{
				mode = Nyx::EStartupBannerMode::Lightning;
			}
			else if (argument == "--reality-cut")
			{
				mode = Nyx::EStartupBannerMode::RealityCut;
			}
			else if (argument == "--chasm")
			{
				mode = Nyx::EStartupBannerMode::Chasm;
			}
			else if (argument == "--rift")
			{
				mode = Nyx::EStartupBannerMode::Rift;
			}
			else if (argument == "--assemble")
			{
				mode = Nyx::EStartupBannerMode::Assemble;
			}
			else if (argument == "--assemble-v2")
			{
				mode = Nyx::EStartupBannerMode::AssembleV2;
			}
			else if (argument == "--benchmark")
			{
				bBenchmark = true;
			}
			else if (argument == "--print-artwork-path")
			{
				bPrintArtworkPath = true;
			}
			else if (argument == "--print-mode")
			{
				bPrintMode = true;
			}
			else if (argument == "--export-frames")
			{
				if (++index >= argc)
				{
					throw std::runtime_error("--export-frames requires an output directory");
				}
				exportDirectory = std::filesystem::absolute(argv[index]);
			}
			else if (argument.starts_with("--") || !artworkDirectory.empty())
			{
				throw std::runtime_error("Unknown option or extra artwork directory");
			}
			else
			{
				artworkDirectory = std::filesystem::absolute(argv[index]);
			}
		}

		if (artworkDirectory.empty())
		{
			artworkDirectory = Nyx::Paths::GetAssetsDir() / "Startup";
		}
		if (mode == Nyx::EStartupBannerMode::Configured)
		{
			mode = Nyx::WindowsStartupBanner::ReadConfiguredMode(artworkDirectory);
		}
		if (bPrintArtworkPath)
		{
			std::cout << artworkDirectory.string() << '\n';
			return 0;
		}
		if (bPrintMode)
		{
			std::cout << GetModeName(mode) << '\n';
			return 0;
		}
		if (!exportDirectory.empty())
		{
			Nyx::WindowsStartupBanner::ExportIntroFrames(artworkDirectory, exportDirectory, mode);
			return 0;
		}
		if (bBenchmark)
		{
			Nyx::WindowsStartupBanner::Benchmark(artworkDirectory, 640, 360, mode);
			Nyx::WindowsStartupBanner::Benchmark(artworkDirectory, 1280, 720, mode);
			return 0;
		}

		Nyx::WindowsStartupBanner banner(artworkDirectory, mode);
		banner.SetStatus(L"Preview: creating the editor window");
		std::this_thread::sleep_for(std::chrono::seconds(7));
		banner.SetStatus(L"Preview: preparing rendering resources");
		std::this_thread::sleep_for(std::chrono::seconds(7));
		banner.SetStatus(L"Preview: preparing the scene and editor panels");
		std::this_thread::sleep_for(std::chrono::seconds(7));
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}

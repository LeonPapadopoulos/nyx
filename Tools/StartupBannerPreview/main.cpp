#include "WindowsStartupBanner.h"
#include "Paths.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

// Optional visual-review tool: holds the real banner open without starting Vulkan.
int main(int argc, char** argv)
{
	// Resolve from the executable, just like the editor. Explorer and shortcuts
	// do not guarantee that the working directory is the repository root.
	const bool bPrintArtworkPath = argc > 1 && std::string_view(argv[1]) == "--print-artwork-path";
	const bool bBenchmark = argc > 1 && std::string_view(argv[1]) == "--benchmark";
	std::filesystem::path artworkDirectory;
	try
	{
		if (bBenchmark && argc > 2)
		{
			artworkDirectory = std::filesystem::absolute(argv[2]);
		}
		else if (argc > 1 && !bPrintArtworkPath && !bBenchmark)
		{
			artworkDirectory = std::filesystem::absolute(argv[1]);
		}
		else
		{
			artworkDirectory = Nyx::Paths::GetAssetsDir() / "Startup";
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	// Headless diagnostic for verifying discovery without opening the banner.
	if (bPrintArtworkPath)
	{
		std::cout << artworkDirectory.string() << '\n';
		return 0;
	}
	if (bBenchmark)
	{
		Nyx::WindowsStartupBanner::Benchmark(artworkDirectory, 640, 360);
		Nyx::WindowsStartupBanner::Benchmark(artworkDirectory, 1280, 720);
		return 0;
	}
	Nyx::WindowsStartupBanner banner(artworkDirectory);
	banner.SetStatus(L"Preview: creating the editor window");
	std::this_thread::sleep_for(std::chrono::seconds(7));
	banner.SetStatus(L"Preview: preparing rendering resources");
	std::this_thread::sleep_for(std::chrono::seconds(7));
	banner.SetStatus(L"Preview: preparing the scene and editor panels");
	std::this_thread::sleep_for(std::chrono::seconds(7));
}

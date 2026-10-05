#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace Nyx
{
	// Independent of GLFW/Vulkan so animation continues during renderer startup.
	// Construction starts the animation thread; destruction fades out and joins it.
	class WindowsStartupBanner
	{
	public:
		explicit WindowsStartupBanner(const std::filesystem::path& artworkDirectory);
		~WindowsStartupBanner();

		WindowsStartupBanner(const WindowsStartupBanner&) = delete;
		WindowsStartupBanner& operator=(const WindowsStartupBanner&) = delete;

		// Safe to call from the editor's initialization thread.
		void SetStatus(std::wstring status);

#ifdef NYX_STARTUP_PREVIEW
		// Headless performance diagnostic, compiled only into the preview tool.
		static void Benchmark(const std::filesystem::path& artworkDirectory, int width, int height);
#endif

	private:
		struct Impl;
		std::unique_ptr<Impl> State;
	};
}

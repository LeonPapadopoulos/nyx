#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace Nyx
{
	enum class EStartupBannerMode
	{
		Configured,
		Classic,
		Lightning,
		RealityCut,
		Chasm,
		Rift,
		Assemble,
		AssembleV2,
		Cinematic = Lightning // Compatibility with the original cinematic setting.
	};

	// Independent of GLFW/Vulkan so animation continues during renderer startup.
	// Construction starts the animation thread; destruction fades out and joins it.
	class WindowsStartupBanner
	{
	public:
		explicit WindowsStartupBanner(
			const std::filesystem::path& artworkDirectory,
			EStartupBannerMode mode = EStartupBannerMode::Configured);
		~WindowsStartupBanner();

		WindowsStartupBanner(const WindowsStartupBanner&) = delete;
		WindowsStartupBanner& operator=(const WindowsStartupBanner&) = delete;

		// Safe to call from the editor's initialization thread.
		void SetStatus(std::wstring status);
		static EStartupBannerMode ReadConfiguredMode(const std::filesystem::path& artworkDirectory);

#ifdef NYX_STARTUP_PREVIEW
		// Headless performance diagnostic, compiled only into the preview tool.
		static void Benchmark(const std::filesystem::path& artworkDirectory, int width, int height,
			EStartupBannerMode mode = EStartupBannerMode::Classic);
		static void ExportIntroFrames(const std::filesystem::path& artworkDirectory,
			const std::filesystem::path& outputDirectory, EStartupBannerMode mode);
#endif

	private:
		struct Impl;
		std::unique_ptr<Impl> State;
	};
}

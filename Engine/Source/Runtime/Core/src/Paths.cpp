#include "Paths.h"

#include <stdexcept>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

namespace Nyx
{
	std::filesystem::path Paths::GetExecutablePath()
	{
#if defined(_WIN32)
		wchar_t buffer[MAX_PATH];
		const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(std::wstring(buffer, length));
#else
#error Paths::GetExecutablePath() not implemented for this platform yet.
#endif
	}

	std::filesystem::path Paths::GetExecutableDir()
	{
		return GetExecutablePath().parent_path();
	}

	std::filesystem::path Paths::FindProjectRootUncached()
	{
		std::filesystem::path current = GetExecutableDir();

		while (!current.empty())
		{
			const std::filesystem::path assetsDir = current / "Assets";

			if (std::filesystem::exists(assetsDir) && std::filesystem::is_directory(assetsDir))
			{
				return current;
			}

			const std::filesystem::path parent = current.parent_path();
			if (parent == current)
			{
				break;
			}

			current = parent;
		}

		throw std::runtime_error("Could not locate project root containing an Assets directory.");
	}

	std::filesystem::path Paths::GetProjectRoot()
	{
		static const std::filesystem::path cached = FindProjectRootUncached();
		return cached;
	}

	std::filesystem::path Paths::GetAssetsDir()
	{
		return GetProjectRoot() / "Assets";
	}

	std::filesystem::path Paths::GetScenesDir()
	{
		return GetAssetsDir() / "Scenes";
	}

	std::filesystem::path Paths::GetTexturesDir()
	{
		return GetAssetsDir() / "Textures";
	}

	std::filesystem::path Paths::GetMaterialsDir()
	{
		return GetAssetsDir() / "Materials";
	}

	std::filesystem::path Paths::GetMeshesDir()
	{
		return GetAssetsDir() / "Meshes";
	}

	std::filesystem::path Paths::GetEngineDir()
	{
		return GetProjectRoot() / "Engine";
	}

	std::filesystem::path Paths::GetShadersDir()
	{
		return GetEngineDir() / "Shaders";
	}
}
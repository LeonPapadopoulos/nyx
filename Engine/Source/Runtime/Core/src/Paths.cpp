#include "Paths.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

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

	namespace
	{
		constexpr const char* RootsFileName = "NyxRoots.ini";

		std::string_view Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(" \t\r");
			if (first == std::string_view::npos)
			{
				return {};
			}
			const size_t last = text.find_last_not_of(" \t\r");
			return text.substr(first, last - first + 1);
		}

		std::filesystem::path PathFromUtf8(std::string_view text)
		{
			return std::filesystem::path(std::u8string(text.begin(), text.end()));
		}
	}

	std::optional<Paths::Roots> Paths::ReadRootsFile(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			return std::nullopt;
		}

		Roots roots;
		std::string line;
		while (std::getline(stream, line))
		{
			const std::string_view text = Trim(line);
			const size_t equals = text.find('=');
			if (text.empty() || text.front() == '#' || equals == std::string_view::npos)
			{
				continue;
			}

			const std::string_view name = Trim(text.substr(0, equals));
			const std::string_view value = Trim(text.substr(equals + 1));
			if (value.empty())
			{
				continue;
			}

			// A relative folder is taken from the file's own folder, so a build can be moved whole
			const std::filesystem::path folder = (file.parent_path() / PathFromUtf8(value)).lexically_normal();
			if (name == "EngineRoot")
			{
				roots.Engine = folder;
			}
			else if (name == "ProjectRoot")
			{
				roots.Project = folder;
			}
		}

		if (roots.Engine.empty() || roots.Project.empty())
		{
			return std::nullopt;
		}
		return roots;
	}

	std::filesystem::path Paths::FindRootAboveExecutable()
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

	Paths::Roots Paths::FindRootsUncached()
	{
		if (std::optional<Roots> roots = ReadRootsFile(GetExecutableDir() / RootsFileName))
		{
			return *roots;
		}

		// A program without the build's roots file, e.g. one copied elsewhere: one folder for both
		const std::filesystem::path root = FindRootAboveExecutable();
		return Roots{ root, root };
	}

	const Paths::Roots& Paths::GetRoots()
	{
		static const Roots cached = FindRootsUncached();
		return cached;
	}

	std::filesystem::path Paths::GetEngineRoot()
	{
		return GetRoots().Engine;
	}

	std::filesystem::path Paths::GetProjectRoot()
	{
		return GetRoots().Project;
	}

	std::filesystem::path Paths::GetAssetsDir()
	{
		return GetProjectRoot() / "Assets";
	}

	std::filesystem::path Paths::GetEngineAssetsDir()
	{
		return GetEngineRoot() / "Assets";
	}

	const std::vector<std::filesystem::path>& Paths::GetAssetSearchDirs()
	{
		static const std::vector<std::filesystem::path> dirs = []
		{
			std::vector<std::filesystem::path> result{ GetAssetsDir() };
			std::error_code error;
			if (!std::filesystem::equivalent(GetAssetsDir(), GetEngineAssetsDir(), error) && GetAssetsDir() != GetEngineAssetsDir())
			{
				result.push_back(GetEngineAssetsDir());
			}
			return result;
		}();
		return dirs;
	}

	std::filesystem::path Paths::FindAssetDirIn(
		const std::vector<std::filesystem::path>& dirs, const std::filesystem::path& relative)
	{
		for (const std::filesystem::path& dir : dirs)
		{
			std::error_code error;
			if (std::filesystem::exists(dir / relative, error))
			{
				return dir;
			}
		}
		return dirs.empty() ? std::filesystem::path() : dirs.front();
	}

	std::filesystem::path Paths::FindAssetDir(const std::filesystem::path& relative)
	{
		return FindAssetDirIn(GetAssetSearchDirs(), relative);
	}

	std::filesystem::path Paths::FindAsset(const std::filesystem::path& relative)
	{
		return FindAssetDir(relative) / relative;
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
		return GetEngineRoot() / "Engine";
	}

	std::filesystem::path Paths::GetShadersDir()
	{
		return GetEngineDir() / "Shaders";
	}

	std::filesystem::path Paths::GetSavedDir()
	{
		return GetProjectRoot() / "Saved";
	}

	std::filesystem::path Paths::GetShaderCacheDir()
	{
		return GetSavedDir() / "ShaderCache";
	}

	std::filesystem::path Paths::GetShaderLibraryDir()
	{
		return GetShadersDir() / "Library";
	}

	std::filesystem::path Paths::GetCompiledShadersDir()
	{
		return GetExecutableDir() / "Shaders";
	}
}
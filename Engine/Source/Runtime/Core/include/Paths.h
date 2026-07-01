#pragma once

#include <filesystem>

namespace Nyx
{
	class Paths
	{
	public:
		static std::filesystem::path GetExecutablePath();
		static std::filesystem::path GetExecutableDir();

		static std::filesystem::path GetProjectRoot();

		static std::filesystem::path GetAssetsDir();
		static std::filesystem::path GetScenesDir();
		static std::filesystem::path GetTexturesDir();
		static std::filesystem::path GetMaterialsDir();
		static std::filesystem::path GetMeshesDir();

		static std::filesystem::path GetEngineDir();
		static std::filesystem::path GetShadersDir();

	private:
		static std::filesystem::path FindProjectRootUncached();
	};
}
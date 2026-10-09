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

		// Shader sources (.vert, .frag): Engine/Shaders
		static std::filesystem::path GetShadersDir();

		// Compiled shaders (.spv): the "Shaders" folder next to the executable. The build copies them
		// there for every program that uses them, so they don't depend on the working directory.
		static std::filesystem::path GetCompiledShadersDir();

	private:
		static std::filesystem::path FindProjectRootUncached();
	};
}
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace Nyx
{
	// Where things are. Two roots:
	// - the engine root, the Nyx folder: the engine's shaders, the editor's themes, the engine's
	//   own assets;
	// - the project root, the folder of what is being made: its assets and what programs keep. In
	//   Nyx's own build both are the Nyx folder; in a game's build the project root is the game's.
	// The build writes both into NyxRoots.ini next to the programs. Without that file, both are the
	// first folder above the executable with an Assets folder.
	class Paths
	{
	public:
		static std::filesystem::path GetExecutablePath();
		static std::filesystem::path GetExecutableDir();

		static std::filesystem::path GetEngineRoot();
		static std::filesystem::path GetProjectRoot();

		// The project's assets: where the editor makes and saves them
		static std::filesystem::path GetAssetsDir();
		static std::filesystem::path GetScenesDir();
		static std::filesystem::path GetTexturesDir();
		static std::filesystem::path GetMaterialsDir();
		static std::filesystem::path GetMeshesDir();

		// The engine's own assets, e.g. the standard material shader and the startup banner
		static std::filesystem::path GetEngineAssetsDir();

		// Where an asset's path is looked for, in order: the project's assets, then the engine's
		// (once, when they are the same folder)
		static const std::vector<std::filesystem::path>& GetAssetSearchDirs();
		// The folder of GetAssetSearchDirs() that has the file or folder `relative`; the project's
		// assets if none has it
		static std::filesystem::path FindAssetDir(const std::filesystem::path& relative);
		// The file or folder `relative`, in the first asset folder that has it
		static std::filesystem::path FindAsset(const std::filesystem::path& relative);

		static std::filesystem::path GetEngineDir();

		// Shader sources (.vert, .frag): Engine/Shaders
		static std::filesystem::path GetShadersDir();

		// What programs keep between runs, e.g. the UI gallery's PNGs: Saved, in the project
		static std::filesystem::path GetSavedDir();

		// What live reload compiled, and the pipeline cache: Saved/ShaderCache
		static std::filesystem::path GetShaderCacheDir();

		// The shader library (nyx/surface.slang, ...): Engine/Shaders/Library. Materials import from it.
		static std::filesystem::path GetShaderLibraryDir();

		// Compiled shaders, as a shader cache (ShaderCache): the "Shaders" folder next to the
		// executable. The build copies them there for every program that uses them, so they don't
		// depend on the working directory.
		static std::filesystem::path GetCompiledShadersDir();

		// The roots as NyxRoots.ini names them
		struct Roots
		{
			std::filesystem::path Engine;
			std::filesystem::path Project;
		};

		// Reads a roots file: UTF-8 lines "EngineRoot = <folder>" and "ProjectRoot = <folder>", blank
		// lines and lines starting with # ignored. Nothing if the file can't be read or lacks a root.
		static std::optional<Roots> ReadRootsFile(const std::filesystem::path& file);

		// FindAssetDir() over the folders given, for tests
		static std::filesystem::path FindAssetDirIn(
			const std::vector<std::filesystem::path>& dirs, const std::filesystem::path& relative);

	private:
		static const Roots& GetRoots();
		static Roots FindRootsUncached();
		static std::filesystem::path FindRootAboveExecutable();
	};
}

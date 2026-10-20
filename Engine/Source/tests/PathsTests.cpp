// Where things are (Paths): the roots file the build writes next to the programs, and assets looked
// for in the project's folder first, then the engine's.
#include "Paths.h"
#include "TestSupport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	namespace fs = std::filesystem;

	using Nyx::Paths;
	using Nyx::Tests::Require;

	void WriteFile(const fs::path& file, const std::string& text)
	{
		fs::create_directories(file.parent_path());
		std::ofstream stream(file, std::ios::binary);
		stream << text;
	}

	void TestRootsFile(const fs::path& directory)
	{
		const fs::path file = directory / "NyxRoots.ini";

		WriteFile(file, "# Written by the build\r\nEngineRoot = C:/Engine Folder\r\n\r\nProjectRoot=C:/Game\r\n");
		std::optional<Paths::Roots> roots = Paths::ReadRootsFile(file);
		Require(roots.has_value(), "A roots file with both roots should be read");
		Require(roots->Engine == fs::path("C:/Engine Folder").lexically_normal(), "The engine root was read wrong");
		Require(roots->Project == fs::path("C:/Game").lexically_normal(), "The project root was read wrong");

		WriteFile(file, "EngineRoot = ../Nyx\nProjectRoot = ..\n");
		roots = Paths::ReadRootsFile(file);
		Require(roots.has_value(), "Relative roots should be read");
		Require(roots->Engine == (directory / "../Nyx").lexically_normal(),
			"A relative root should be taken from the file's folder");
		Require(roots->Project == (directory / "..").lexically_normal(), "A relative project root was read wrong");

		WriteFile(file, "EngineRoot = C:/Engine\n");
		Require(!Paths::ReadRootsFile(file).has_value(), "A roots file without the project root should be refused");

		Require(!Paths::ReadRootsFile(directory / "Missing.ini").has_value(), "A missing roots file should give nothing");
	}

	void TestAssetsAreFoundInTheProjectFirst(const fs::path& directory)
	{
		const fs::path project = directory / "Project" / "Assets";
		const fs::path engine = directory / "Engine" / "Assets";
		WriteFile(project / "Materials" / "Stone.nyxmat", "project");
		WriteFile(engine / "Materials" / "Stone.nyxmat", "engine");
		WriteFile(engine / "Shaders" / "Standard.slang", "engine");
		const std::vector<fs::path> dirs{ project, engine };

		Require(Paths::FindAssetDirIn(dirs, "Materials/Stone.nyxmat") == project,
			"An asset in both folders should be the project's");
		Require(Paths::FindAssetDirIn(dirs, "Shaders/Standard.slang") == engine,
			"An asset only the engine has should be the engine's");
		Require(Paths::FindAssetDirIn(dirs, "Shaders") == engine, "Folders should be found too");
		Require(Paths::FindAssetDirIn(dirs, "Materials/Missing.nyxmat") == project,
			"An asset nobody has should be looked for in the project, where it would be made");
	}

	void TestThisBuildsRoots()
	{
		// Nyx's own build: the roots file next to this program names the Nyx folder for both
		Require(fs::exists(Paths::GetExecutableDir() / "NyxRoots.ini"), "The build should write NyxRoots.ini next to the programs");
		Require(fs::is_directory(Paths::GetEngineDir() / "Shaders"), "The engine root should have the engine's shaders");
		Require(fs::is_directory(Paths::GetAssetsDir()), "The project root should have an Assets folder");
		Require(Paths::GetAssetSearchDirs().size() == 1, "In Nyx's own build the project's and the engine's assets are one folder");
		Require(fs::exists(Paths::FindAsset("Shaders/Standard.slang")), "The standard material shader should be found");
	}
}

int main()
{
	try
	{
		Nyx::Tests::TestDirectory directory("NyxPathsTests");
		TestRootsFile(directory.Path);
		TestAssetsAreFoundInTheProjectFirst(directory.Path);
		TestThisBuildsRoots();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Paths test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All paths tests passed.\n";
	return 0;
}

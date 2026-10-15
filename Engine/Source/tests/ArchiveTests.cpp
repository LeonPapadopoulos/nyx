#include "BinaryArchive.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	namespace fs = std::filesystem;

	using Nyx::Tests::Require;
	using Nyx::Tests::TestDirectory;

	// The file is still writable, but cannot be renamed or deleted while this handle is open.
	// An unsafe writer that truncates the destination directly would incorrectly succeed.
	class PreventReplacement
	{
	public:
		explicit PreventReplacement(const fs::path& path)
		{
			File = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			Require(File != INVALID_HANDLE_VALUE, "Could not lock the destination against replacement");
		}

		~PreventReplacement()
		{
			CloseHandle(File);
		}

	private:
		HANDLE File = INVALID_HANDLE_VALUE;
	};

	std::vector<char> ReadFile(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		Require(file.is_open(), "Could not read the saved file");
		return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	void RequireNoTemporaryFiles(const fs::path& directory)
	{
		for (const auto& entry : fs::directory_iterator(directory))
		{
			Require(!entry.path().filename().wstring().starts_with(L".nyx-save-"), "Temporary save file was left behind");
		}
	}

	BinaryWriter MakeWriter(const std::vector<char>& bytes)
	{
		BinaryWriter writer;
		writer.WriteBytes(bytes.data(), bytes.size());
		return writer;
	}

	void TestCreateReplaceAndEmpty(const fs::path& directory)
	{
		const fs::path path = directory / L"scene with spaces \u6708.nyxscene";
		std::vector<char> large(2 * 1024 * 1024 + 37);
		for (size_t i = 0; i < large.size(); ++i)
		{
			large[i] = static_cast<char>(i % 127);
		}

		Require(MakeWriter(large).SaveToFile(path), "Creating a file failed");
		Require(ReadFile(path) == large, "Large archive bytes changed during saving");

		const std::vector<char> replacement{ 'n', 'e', 'w', '\0', '!' };
		Require(MakeWriter(replacement).SaveToFile(path), "Replacing a file failed");
		Require(ReadFile(path) == replacement, "Replacement kept stale bytes or changed content");

		Require(BinaryWriter{}.SaveToFile(path), "Saving an empty archive failed");
		Require(ReadFile(path).empty(), "Empty archive did not replace the previous contents");
		RequireNoTemporaryFiles(directory);
	}

	void TestFailedReplacement(const fs::path& directory)
	{
		const fs::path path = directory / "locked.nyxscene";
		const std::vector<char> original{ 'k', 'e', 'e', 'p' };
		const BinaryWriter replacement = MakeWriter({ 'r', 'e', 'p', 'l', 'a', 'c', 'e' });
		Require(MakeWriter(original).SaveToFile(path), "Could not create the original file");

		{
			PreventReplacement lock(path);
			Require(!replacement.SaveToFile(path), "Saving to a locked destination incorrectly succeeded");
			Require(ReadFile(path) == original, "Failed replacement changed the original file");
			RequireNoTemporaryFiles(directory);
		}

		Require(replacement.SaveToFile(path), "Retrying after a failed replacement did not succeed");
		Require(ReadFile(path) == std::vector<char>({ 'r', 'e', 'p', 'l', 'a', 'c', 'e' }), "Retry saved the wrong bytes");
		RequireNoTemporaryFiles(directory);
	}

	void TestInvalidDestinations(const fs::path& directory)
	{
		const BinaryWriter writer = MakeWriter({ 'd', 'a', 't', 'a' });
		Require(!writer.SaveToFile({}), "An empty path was accepted");
		Require(!writer.SaveToFile(directory / "missing" / "scene.nyxscene"), "A missing parent directory was accepted");

		const fs::path childDirectory = directory / "existing-directory";
		fs::create_directory(childDirectory);
		Require(!writer.SaveToFile(childDirectory), "A directory was replaced with a file");
		Require(fs::is_directory(childDirectory), "Failed save damaged the destination directory");
		RequireNoTemporaryFiles(directory);
	}

	void TestSceneSaveFailure(const fs::path& directory)
	{
		const fs::path path = directory / "world.nyxscene";
		Registry world;
		const Entity entity = world.CreateEntity();
		const EntityGuid guid = EntityGuid::Generate();
		world.Add<GuidComponent>(entity, GuidComponent{ guid });
		world.Add<NameComponent>(entity, NameComponent{ "Original" });
		Require(SceneSerializer::SaveToFile(world, path), "Initial scene save failed");
		const std::vector<char> original = ReadFile(path);

		world.Get<NameComponent>(entity).Name = "Edited";
		{
			PreventReplacement lock(path);
			Require(!SceneSerializer::SaveToFile(world, path), "SceneSerializer did not propagate the save failure");
			Require(ReadFile(path) == original, "Failed scene save changed the original bytes");
			RequireNoTemporaryFiles(directory);
		}

		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "The original scene no longer loads");
		const auto restored = FindEntityByGuid(loaded, guid);
		Require(restored.has_value(), "The original scene lost its entity");
		Require(loaded.Get<NameComponent>(*restored).Name == "Original", "A failed save changed the scene");

		Require(SceneSerializer::SaveToFile(world, path), "Scene save retry failed");
		Require(SceneSerializer::LoadFromFile(path, loaded), "The replacement scene does not load");
		const auto edited = FindEntityByGuid(loaded, guid);
		Require(edited.has_value() && loaded.Get<NameComponent>(*edited).Name == "Edited", "Scene replacement lost edits");
		RequireNoTemporaryFiles(directory);
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestDirectory directory("NyxArchiveTests");
		TestCreateReplaceAndEmpty(directory.Path);
		TestFailedReplacement(directory.Path);
		TestInvalidDestinations(directory.Path);
		TestSceneSaveFailure(directory.Path);
		std::cout << "All archive save tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Archive save test failed: " << error.what() << '\n';
		return 1;
	}
}

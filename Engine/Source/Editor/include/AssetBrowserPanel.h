#pragma once

#include "AssetDatabase.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Nyx::Editor
{
	class AssetBrowserPanel
	{
	public:
		using OpenSceneCallback = std::function<void(const std::filesystem::path&)>;

		void SetDatabase(AssetDatabase* database)
		{
			Database = database;
		}

		void SetOpenSceneCallback(OpenSceneCallback callback)
		{
			OnOpenScene = std::move(callback);
		}

		void Draw();

		const std::filesystem::path& GetCurrentDirectory() const
		{
			return CurrentDirectory;
		}

		void SetCurrentDirectory(const std::filesystem::path& relativeDirectory)
		{
			CurrentDirectory = relativeDirectory;
		}

	private:
		void DrawDirectoryTree(const std::filesystem::path& relativeDirectory);
		void DrawDirectoryContents();

	private:
		AssetDatabase* Database = nullptr;
		OpenSceneCallback OnOpenScene;
		std::filesystem::path CurrentDirectory;
	};
}
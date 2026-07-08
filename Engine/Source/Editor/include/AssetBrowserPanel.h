#pragma once

#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"

#include <filesystem>

namespace Nyx::Editor
{
	class AssetBrowserPanel
	{
	public:
		void SetDatabase(AssetDatabase* database)
		{
			Database = database;
		}

		void SetActivationContext(IAssetActivationContext* context)
		{
			ActivationContext = context;
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
		IAssetActivationContext* ActivationContext = nullptr;
		std::filesystem::path CurrentDirectory;
	};
}
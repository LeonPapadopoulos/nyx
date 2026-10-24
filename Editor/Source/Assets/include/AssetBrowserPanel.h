#pragma once

#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Nyx::Editor
{
	class AssetBrowserPanel
	{
	public:
		void SetDatabase(AssetDatabase* database)
		{
			Database = database;
		}

		// The types whose files a double click opens (AssetTypeDescriptor::Activate)
		void SetAssetTypes(const AssetTypeRegistry* types)
		{
			Types = types;
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

		// What the right-click menus do: New Material in the folder's menu, New Material for
		// Shader in a shader's. The folder's menu also has New <DisplayName> for each asset type that
		// can make one (AssetTypeDescriptor::Create).
		std::function<void()> NewMaterial;
		std::function<void(const std::string& shaderPath)> NewMaterialForShader;
		// New Material Instance in a material's menu
		std::function<void(const std::string& materialPath)> NewMaterialInstance;

	private:
		void DrawDirectoryTree(const std::filesystem::path& relativeDirectory);
		void DrawDirectoryContents();

	private:
		AssetDatabase* Database = nullptr;
		const AssetTypeRegistry* Types = nullptr;
		std::filesystem::path CurrentDirectory;
		// The asset or folder clicked last, shown selected
		std::filesystem::path SelectedAsset;
	};
}
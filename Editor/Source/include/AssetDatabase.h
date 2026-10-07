#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	struct AssetEntry
	{
		std::filesystem::path RelativePath;
		std::filesystem::path AbsolutePath;
		std::string Name;
		std::string TypeId; // "Directory" for folders, otherwise registered asset type
		bool bIsDirectory = false;

		bool IsValidAsset() const
		{
			return !bIsDirectory && !TypeId.empty();
		}
	};

	class AssetDatabase
	{
	public:
		void SetAssetRoot(const std::filesystem::path& assetRoot);
		void Rescan();

		const std::filesystem::path& GetAssetRoot() const
		{
			return AssetRoot;
		}

		bool IsValidAssetRoot() const;

		std::vector<AssetEntry> GetChildren(const std::filesystem::path& relativeDirectory) const;
		std::vector<AssetEntry> GetAllByType(std::string_view typeId) const;

	private:
		AssetEntry BuildEntry(const std::filesystem::directory_entry& entry) const;

	private:
		std::filesystem::path AssetRoot;
		std::vector<AssetEntry> Entries;
	};
}
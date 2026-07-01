#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	enum class EAssetEntryType
	{
		Directory,
		Scene,
		UnknownFile
	};

	struct AssetEntry
	{
		std::filesystem::path RelativePath;
		std::filesystem::path AbsolutePath;
		std::string Name;
		EAssetEntryType Type = EAssetEntryType::UnknownFile;
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

		std::vector<AssetEntry> GetChildren(const std::filesystem::path& relativeDirectory) const;
		bool IsValidAssetRoot() const;

	private:
		static EAssetEntryType ClassifyPath(const std::filesystem::path& path);

	private:
		std::filesystem::path AssetRoot;
		std::vector<AssetEntry> Entries;
	};
}
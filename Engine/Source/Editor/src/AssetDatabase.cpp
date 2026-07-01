#include "AssetDatabase.h"

#include <algorithm>

namespace Nyx::Editor
{
	void AssetDatabase::SetAssetRoot(const std::filesystem::path& assetRoot)
	{
		AssetRoot = assetRoot;
	}

	bool AssetDatabase::IsValidAssetRoot() const
	{
		return !AssetRoot.empty() && std::filesystem::exists(AssetRoot) && std::filesystem::is_directory(AssetRoot);
	}

	EAssetEntryType AssetDatabase::ClassifyPath(const std::filesystem::path& path)
	{
		if (std::filesystem::is_directory(path))
		{
			return EAssetEntryType::Directory;
		}

		if (path.extension() == ".nyxscene")
		{
			return EAssetEntryType::Scene;
		}

		return EAssetEntryType::UnknownFile;
	}

	void AssetDatabase::Rescan()
	{
		Entries.clear();

		if (!IsValidAssetRoot())
		{
			return;
		}

		for (const auto& entry : std::filesystem::recursive_directory_iterator(AssetRoot))
		{
			AssetEntry assetEntry{};
			assetEntry.AbsolutePath = entry.path();
			assetEntry.RelativePath = std::filesystem::relative(entry.path(), AssetRoot);
			assetEntry.Name = entry.path().filename().string();
			assetEntry.Type = ClassifyPath(entry.path());

			Entries.push_back(std::move(assetEntry));
		}

		std::sort(Entries.begin(), Entries.end(), [](const AssetEntry& a, const AssetEntry& b)
			{
				if (a.RelativePath.parent_path() != b.RelativePath.parent_path())
				{
					return a.RelativePath.parent_path().string() < b.RelativePath.parent_path().string();
				}

				if (a.Type != b.Type)
				{
					// Directories first
					if (a.Type == EAssetEntryType::Directory) return true;
					if (b.Type == EAssetEntryType::Directory) return false;
				}

				return a.Name < b.Name;
			});
	}

	std::vector<AssetEntry> AssetDatabase::GetChildren(const std::filesystem::path& relativeDirectory) const
	{
		std::vector<AssetEntry> result;

		for (const AssetEntry& entry : Entries)
		{
			if (entry.RelativePath.parent_path() == relativeDirectory)
			{
				result.push_back(entry);
			}
		}

		return result;
	}
}
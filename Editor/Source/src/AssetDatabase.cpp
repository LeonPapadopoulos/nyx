#include "AssetDatabase.h"

#include "AssetTypeRegistry.h"

#include <algorithm>

namespace Nyx::Editor
{
	void AssetDatabase::SetAssetRoot(const std::filesystem::path& assetRoot)
	{
		AssetRoot = assetRoot;
	}

	bool AssetDatabase::IsValidAssetRoot() const
	{
		return !AssetRoot.empty() &&
			std::filesystem::exists(AssetRoot) &&
			std::filesystem::is_directory(AssetRoot);
	}

	AssetEntry AssetDatabase::BuildEntry(const std::filesystem::directory_entry& entry) const
	{
		AssetEntry result{};
		result.AbsolutePath = entry.path();
		result.RelativePath = std::filesystem::relative(entry.path(), AssetRoot);
		result.Name = entry.path().filename().string();
		result.bIsDirectory = entry.is_directory();

		if (result.bIsDirectory)
		{
			result.TypeId = "Directory";
			return result;
		}

		const std::string extension = entry.path().extension().string();
		if (const AssetTypeDescriptor* descriptor = AssetTypeRegistry::Get().FindByExtension(extension))
		{
			result.TypeId = descriptor->TypeId;
		}
		else
		{
			result.TypeId.clear();
		}

		return result;
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
			Entries.push_back(BuildEntry(entry));
		}

		std::sort(
			Entries.begin(),
			Entries.end(),
			[](const AssetEntry& a, const AssetEntry& b)
			{
				if (a.RelativePath.parent_path() != b.RelativePath.parent_path())
				{
					return a.RelativePath.parent_path().string() < b.RelativePath.parent_path().string();
				}

				if (a.bIsDirectory != b.bIsDirectory)
				{
					return a.bIsDirectory;
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

	std::vector<AssetEntry> AssetDatabase::GetAllByType(std::string_view typeId) const
	{
		std::vector<AssetEntry> result;

		for (const AssetEntry& entry : Entries)
		{
			if (!entry.bIsDirectory && entry.TypeId == typeId)
			{
				result.push_back(entry);
			}
		}

		return result;
	}
}
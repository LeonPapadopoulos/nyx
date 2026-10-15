#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Editor
{
	struct AssetEntry;
	class EditorLayer;

	using AssetActivateFn = std::function<void(const AssetEntry&)>;
	using AssetCanActivateFn = std::function<bool(const AssetEntry&)>;

	struct AssetTypeDescriptor
	{
		std::string TypeId;                  // e.g. "Scene", "Mesh", "Material"
		std::string DisplayName;             // e.g. "Scene"
		std::vector<std::string> Extensions; // e.g. { ".nyxscene" }

		bool bBrowsable = true;
		bool bOpenOnDoubleClick = false;

		AssetCanActivateFn CanActivate;
		AssetActivateFn Activate;
	};

	class AssetTypeRegistry
	{
	public:
		void Register(AssetTypeDescriptor descriptor);

		const AssetTypeDescriptor* FindByTypeId(std::string_view typeId) const;
		const AssetTypeDescriptor* FindByExtension(std::string_view extension) const;
		const std::vector<AssetTypeDescriptor>& GetAll() const
		{
			return Types;
		}

	private:
		std::vector<AssetTypeDescriptor> Types;
	};

	// Every asset type the editor knows (EditorAssetTypes.cpp). Opening one calls into `editor`,
	// which must outlive the registry's use.
	void RegisterEditorAssetTypes(AssetTypeRegistry& registry, EditorLayer& editor);
}
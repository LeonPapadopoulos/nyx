#include "AssetTypeRegistry.h"

#include "AssetDatabase.h"
#include "EditorLayer.h"
#include "ReflectedAssetTypes.h"

namespace Nyx::Editor
{
	void RegisterEditorAssetTypes(AssetTypeRegistry& registry, EditorLayer& editor)
	{
		registry.Register(AssetTypeDescriptor{
			.TypeId = "Scene",
			.DisplayName = "Scene",
			.Extensions = { ".nyxscene" },
			.bBrowsable = true,
			.bOpenOnDoubleClick = true,
			.CanActivate = [](const AssetEntry& entry)
			{
				return entry.TypeId == "Scene";
			},
			.Activate = [&editor](const AssetEntry& entry)
			{
				editor.LoadCurrentScene(entry.AbsolutePath);
			} });

		registry.Register(AssetTypeDescriptor{
			.TypeId = "Mesh",
			.DisplayName = "Mesh",
			.Extensions = { ".nyxmesh" },
			.bBrowsable = true,
			.bOpenOnDoubleClick = false });

		// Opens it in the Parameters window
		registry.Register(AssetTypeDescriptor{
			.TypeId = "Material",
			.DisplayName = "Material",
			.Extensions = { ".nyxmat" },
			.bBrowsable = true,
			.bOpenOnDoubleClick = true,
			.Activate = [&editor](const AssetEntry& entry)
			{
				editor.OpenMaterial(entry.RelativePath.generic_string());
			} });

		// The asset types edited by reflection, the engine's (physics materials) and the game module's:
		// opened in a window of their own, made new from the asset browser's folder menu
		for (const Nyx::ReflectedAssetType& type : Nyx::ReflectedAssetTypes::Get().GetAll())
		{
			registry.Register(AssetTypeDescriptor{
				.TypeId = type.TypeId,
				.DisplayName = type.DisplayName,
				.Extensions = { type.Extension },
				.bBrowsable = true,
				.bOpenOnDoubleClick = true,
				.Activate = [&editor](const AssetEntry& entry)
				{
					editor.OpenReflectedAsset(entry.RelativePath.generic_string());
				},
				.Create = [&editor, &type](const std::filesystem::path& folder)
				{
					editor.CreateReflectedAsset(type, folder);
				},
				.Description = type.Description });
		}

		// Opens it in the source editor: shaders are written there
		registry.Register(AssetTypeDescriptor{
			.TypeId = "Shader",
			.DisplayName = "Shader",
			.Extensions = { ".slang" },
			.bBrowsable = true,
			.bOpenOnDoubleClick = true,
			.Activate = [&editor](const AssetEntry& entry)
			{
				editor.OpenShader(entry.AbsolutePath);
			} });
	}
}

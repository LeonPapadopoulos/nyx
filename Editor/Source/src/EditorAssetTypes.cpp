#include "AssetTypeRegistry.h"

#include "AssetDatabase.h"
#include "EditorLayer.h"

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

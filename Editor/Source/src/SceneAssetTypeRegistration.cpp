#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"
#include "EditorAssetActivationContext.h"
#include "EditorLayer.h"

namespace Nyx::Editor
{
	namespace
	{
		bool CanActivateSceneAsset(const AssetEntry& entry, IAssetActivationContext& context)
		{
			return entry.TypeId == "Scene";
		}

		void ActivateSceneAsset(const AssetEntry& entry, IAssetActivationContext& context)
		{
			EditorAssetActivationContext& editorContext =
				static_cast<EditorAssetActivationContext&>(context);

			editorContext.Editor.LoadCurrentScene(entry.AbsolutePath);
		}

		AssetTypeRegistrar GSceneAssetTypeRegistrar(
			AssetTypeDescriptor{
				.TypeId = "Scene",
				.DisplayName = "Scene",
				.Extensions = { ".nyxscene" },
				.bBrowsable = true,
				.bOpenOnDoubleClick = true,
				.CanActivate = &CanActivateSceneAsset,
				.Activate = &ActivateSceneAsset });
	}
}
#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"
#include "EditorAssetActivationContext.h"
#include "EditorLayer.h"

namespace Nyx::Editor
{
	namespace
	{
		// Opens it in the Parameters window
		void ActivateMaterialAsset(const AssetEntry& entry, IAssetActivationContext& context)
		{
			static_cast<EditorAssetActivationContext&>(context).Editor.OpenMaterial(entry.RelativePath.generic_string());
		}

		AssetTypeRegistrar GMaterialAssetTypeRegistrar(
			AssetTypeDescriptor{
				.TypeId = "Material",
				.DisplayName = "Material",
				.Extensions = { ".nyxmat" },
				.bBrowsable = true,
				.bOpenOnDoubleClick = true,
				.Activate = &ActivateMaterialAsset });
	}
}
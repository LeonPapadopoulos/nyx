#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"
#include "EditorAssetActivationContext.h"
#include "EditorLayer.h"

namespace Nyx::Editor
{
	namespace
	{
		// Opens it in the source editor: shaders are written there
		void ActivateShaderAsset(const AssetEntry& entry, IAssetActivationContext& context)
		{
			static_cast<EditorAssetActivationContext&>(context).Editor.OpenShader(entry.AbsolutePath);
		}

		AssetTypeRegistrar GShaderAssetTypeRegistrar(
			AssetTypeDescriptor{
				.TypeId = "Shader",
				.DisplayName = "Shader",
				.Extensions = { ".slang" },
				.bBrowsable = true,
				.bOpenOnDoubleClick = true,
				.Activate = &ActivateShaderAsset });
	}
}

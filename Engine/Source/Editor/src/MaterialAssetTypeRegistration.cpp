#include "AssetTypeRegistry.h"

namespace Nyx::Editor
{
	namespace
	{
		AssetTypeRegistrar GMaterialAssetTypeRegistrar(
			AssetTypeDescriptor{
				.TypeId = "Material",
				.DisplayName = "Material",
				.Extensions = { ".nyxmat" },
				.bBrowsable = true,
				.bOpenOnDoubleClick = false
			});
	}
}
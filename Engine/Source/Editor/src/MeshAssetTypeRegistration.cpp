#include "AssetTypeRegistry.h"

namespace Nyx::Editor
{
	namespace
	{
		AssetTypeRegistrar GMeshAssetTypeRegistrar(
			AssetTypeDescriptor{
				.TypeId = "Mesh",
				.DisplayName = "Mesh",
				.Extensions = { ".nyxmesh" },
				.bBrowsable = true,
				.bOpenOnDoubleClick = false
			});
	}
}
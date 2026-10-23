#include "NyxPCH.h"
#include "MeshRendererComponent.h"

namespace Nyx
{
	Nyx::Material* GetSlotMaterial(const MeshRendererComponent& component, uint32_t slot)
	{
		if (slot == 0 || slot > component.ExtraMaterialAssets.size() || !component.ExtraMaterialAssets[slot - 1])
		{
			return component.MaterialAsset;
		}
		return component.ExtraMaterialAssets[slot - 1];
	}
}

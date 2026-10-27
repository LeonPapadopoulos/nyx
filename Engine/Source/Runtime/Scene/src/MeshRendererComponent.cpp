#include "NyxPCH.h"
#include "MeshRendererComponent.h"

#include "Entity.h"
#include "IAssetResolver.h"

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

	void ResolveMeshRendererAssets(MeshRendererComponent& component, IAssetResolver* resolver)
	{
		component.MeshAsset = resolver && component.Mesh.IsValid() ? resolver->ResolveMesh(component.Mesh.Path) : nullptr;
		component.MaterialAsset = resolver && component.Material.IsValid() ? resolver->ResolveMaterial(component.Material.Path) : nullptr;

		component.ExtraMaterialAssets.clear();
		component.ResolvedExtraMaterialPaths.clear();
		for (const MeshMaterialSlot& slot : component.ExtraMaterials)
		{
			component.ExtraMaterialAssets.push_back(resolver && slot.Material.IsValid() ? resolver->ResolveMaterial(slot.Material.Path) : nullptr);
			component.ResolvedExtraMaterialPaths.push_back(slot.Material.Path);
		}
		component.ResolvedMeshPath = component.Mesh.Path;
		component.ResolvedMaterialPath = component.Material.Path;
	}

	void ResolveChangedMeshRenderers(Registry& world, IAssetResolver& resolver)
	{
		world.Each<MeshRendererComponent>(
			[&resolver](Entity, MeshRendererComponent& component)
			{
				bool bCurrent = component.Mesh.Path == component.ResolvedMeshPath && component.Material.Path == component.ResolvedMaterialPath &&
					component.ExtraMaterials.size() == component.ResolvedExtraMaterialPaths.size();
				for (size_t slot = 0; bCurrent && slot < component.ExtraMaterials.size(); ++slot)
				{
					bCurrent = component.ExtraMaterials[slot].Material.Path == component.ResolvedExtraMaterialPaths[slot];
				}
				if (!bCurrent)
				{
					ResolveMeshRendererAssets(component, &resolver);
				}
			});
	}
}

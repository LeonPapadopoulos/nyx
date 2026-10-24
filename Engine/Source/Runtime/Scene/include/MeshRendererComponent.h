#pragma once

#include "ReflectionMacros.h"
#include "AssetReference.h"

#include <cstdint>
#include <string>
#include <vector>

// What the renderer draws the component with; the scene only holds them
namespace Nyx
{
	class Mesh;
	struct Material;
}

namespace Nyx
{
	// A material slot after the first, for a mesh of more than one section (MeshSection)
	NYX_REFLECT()
	struct MeshMaterialSlot
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (AssetType = Material, Tooltip = "What the sections of this slot are drawn with; none: the first slot's material"))
		Nyx::AssetReference Material;
	};

	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Renderer Component"))
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", AssetType = Mesh, Tooltip = "The built-in mesh drawn; meshes become assets later. A mesh made by code while the game runs takes its place"))
		Nyx::AssetReference Mesh;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", AssetType = Material, Tooltip = "What the mesh is drawn with: pick a material, or drag one from the asset browser. For a mesh of sections, the first slot's"))
		Nyx::AssetReference Material;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "Whether the mesh is rendered"))
		bool bVisible = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", DisplayName = "Cast Shadows", Tooltip = "Whether the mesh casts shadows from the primary light; a hidden mesh casts none"))
		bool bCastShadows = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "The materials of slots 1, 2 and on, for a mesh whose sections ask for them, such as a tree's leaves beside its trunk"))
		std::vector<Nyx::MeshMaterialSlot> ExtraMaterials;

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
		// ExtraMaterials' materials, slot 1 first; null for a slot without one
		std::vector<Nyx::Material*> ExtraMaterialAssets;
	};

	// What the sections of `slot` are drawn with: slot 0's is MaterialAsset; a later slot's is its
	// own, or MaterialAsset for a slot without one
	Nyx::Material* GetSlotMaterial(const MeshRendererComponent& component, uint32_t slot);
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"

#pragma once

#include "ReflectionMacros.h"
#include "AssetReference.h"

#include <string>

// What the renderer draws the component with; the scene only holds them
namespace Nyx
{
	class Mesh;
	struct Material;
}

namespace Nyx
{
	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Renderer Component"))
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", AssetType = Mesh, Tooltip = "The built-in mesh drawn; meshes become assets later"))
		Nyx::AssetReference Mesh;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", AssetType = Material, Tooltip = "What the mesh is drawn with: pick a material, or drag one from the asset browser"))
		Nyx::AssetReference Material;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "Whether the mesh is rendered"))
		bool bVisible = true;

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
	};
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"
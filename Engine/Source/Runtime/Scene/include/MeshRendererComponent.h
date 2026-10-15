#pragma once

#include "Mesh.h"
#include "Material.h"
#include "ReflectionMacros.h"
#include "AssetReference.h"

#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Renderer Component"))
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", AssetType = Mesh, Tooltip = "The built-in mesh drawn; meshes become assets later"))
		Nyx::Engine::AssetReference Mesh;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", AssetType = Material, Tooltip = "What the mesh is drawn with: pick a material, or drag one from the asset browser"))
		Nyx::Engine::AssetReference Material;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "Whether the mesh is rendered"))
		bool bVisible = true;

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
	};
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"
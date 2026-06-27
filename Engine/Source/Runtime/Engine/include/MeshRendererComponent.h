#pragma once

#include "Mesh.h"
#include "Material.h"
#include "ReflectionMacros.h"

#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Renderer Component"))
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", Tooltip = "Debug Info"))
		std::string MeshId;

		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", Tooltip = "Debug Info"))
		std::string MaterialId;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "Whether the mesh is rendered"))
		bool bVisible = true;

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
	};
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"
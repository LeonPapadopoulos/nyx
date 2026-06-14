#pragma once

#include "Mesh.h"
#include "Material.h"
#include "ReflectionMacros.h"

#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT()
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string MeshId;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string MaterialId;

		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bVisible = true;

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
	};
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"
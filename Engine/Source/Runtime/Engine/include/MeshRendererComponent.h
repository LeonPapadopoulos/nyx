#pragma once

#include "Mesh.h"
#include "Material.h"

namespace Nyx::Engine
{
	struct MeshRendererComponent
	{
		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
		bool bVisible = true;
	};
}
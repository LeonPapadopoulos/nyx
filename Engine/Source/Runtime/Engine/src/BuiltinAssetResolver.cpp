#include "BuiltinAssetResolver.h"

#include "Renderer.h"

namespace Nyx::Engine
{
	BuiltinAssetResolver::BuiltinAssetResolver(Nyx::IRenderer& renderer)
		: Renderer(renderer)
	{
	}

	Nyx::Mesh* BuiltinAssetResolver::ResolveMesh(const std::string& meshPath)
	{
		if (meshPath == "Meshes/Cube.nyxmesh")
		{
			return Renderer.GetCubeMesh();
		}

		return nullptr;
	}

	Nyx::Material* BuiltinAssetResolver::ResolveMaterial(const std::string& materialPath)
	{
		if (materialPath == "Materials/Textured.nyxmat")
		{
			return Renderer.GetTexturedMaterial();
		}

		if (materialPath == "Materials/Reflective.nyxmat")
		{
			return Renderer.GetReflectiveMaterial();
		}

		if (materialPath == "Materials/Untextured.nyxmat")
		{
			return Renderer.GetUntexturedMaterial();
		}

		return nullptr;
	}
}

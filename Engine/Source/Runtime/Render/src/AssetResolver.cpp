#include "AssetResolver.h"

#include "Renderer.h"

namespace Nyx
{
	AssetResolver::AssetResolver(Nyx::IRenderer& renderer)
		: Renderer(renderer)
	{
	}

	Nyx::Mesh* AssetResolver::ResolveMesh(const std::string& meshPath)
	{
		if (meshPath == "Meshes/Cube.nyxmesh")
		{
			return Renderer.GetCubeMesh();
		}
		if (meshPath == "Meshes/Sphere.nyxmesh")
		{
			return Renderer.GetSphereMesh();
		}
		if (meshPath == "Meshes/Capsule.nyxmesh")
		{
			return Renderer.GetCapsuleMesh();
		}
		if (meshPath == "Meshes/CrossedQuads.nyxmesh")
		{
			return Renderer.GetCrossedQuadsMesh();
		}

		return nullptr;
	}

	Nyx::Material* AssetResolver::ResolveMaterial(const std::string& materialPath)
	{
		// A file that can't be read gives a material all the same, which shows it: magenta
		return Renderer.GetMaterial(materialPath);
	}
}

#include "EditorAssetResolver.h"

#include "VulkanRenderer.h"

namespace Nyx::Editor
{
	Nyx::Mesh* EditorAssetResolver::ResolveMesh(const std::string& meshId)
	{
		if (meshId == "Cube")
		{
			return Renderer.GetCubeMesh();
		}

		return nullptr;
	}

	Nyx::Material* EditorAssetResolver::ResolveMaterial(const std::string& materialId)
	{
		if (materialId == "TexturedMaterial")
		{
			return Renderer.GetTexturedMaterial();
		}

		if (materialId == "ReflectiveMaterial")
		{
			return Renderer.GetReflectiveMaterial();
		}

		if (materialId == "UntexturedMaterial")
		{
			return Renderer.GetUntexturedMaterial();
		}

		return nullptr;
	}
}
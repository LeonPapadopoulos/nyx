#pragma once

#include "Mesh.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Nyx
{
	struct ImportedPrimitive
	{
		MeshData Mesh;
		int MaterialIndex = -1;
	};

	struct ImportedNodeInstance
	{
		int PrimitiveIndex = -1;
		glm::mat4 WorldTransform{ 1.0f };
	};

	struct ImportedMaterial
	{
		std::string BaseColorTexturePath;
		glm::vec4 BaseColorFactor{ 1.0f };
	};

	struct ImportedScene
	{
		std::vector<ImportedPrimitive> Primitives;
		std::vector<ImportedNodeInstance> Instances;
		std::vector<ImportedMaterial> Materials;
	};

	// @todo: actually load a stored scene from disk and show it inside the viewport
	bool LoadStaticGltfScene(const std::string& filePath, ImportedScene& outScene);
}
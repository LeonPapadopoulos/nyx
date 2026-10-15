#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	// A mesh as plain data, built (MakeSphere()) or imported (GltfImporter); Mesh puts it on the GPU
	struct Vertex
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
		glm::vec2 UV{ 0.0f, 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		// Along the texture's u in xyz; w is +1 or -1: the bitangent, cross(normal, tangent) * w,
		// points up the image (towards falling v), where a normal map's green points. For normal
		// maps (ComputeTangents()).
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
	};
}

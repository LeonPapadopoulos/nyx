#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx
{
	// A mesh as plain data: built in code (MakeSphere(), a generator's system), or imported
	// (GltfImporter); the renderer puts it on the GPU (Mesh), physics makes collision shapes of it
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
		// Four numbers of the mesh's own, for the materials (SurfaceInput.Custom and a vertex
		// function's input), such as how far a vertex sways in the wind, or the ages at which it
		// appears and is fully grown. Zero for a mesh that has none. On the GPU in half precision,
		// about three significant digits (GpuVertex): for weights, ages and phases, not positions.
		glm::vec4 Custom{ 0.0f };
	};

	// A run of a mesh's triangles drawn with one material: the one in slot MaterialSlot of whatever
	// draws the mesh (MeshRendererComponent), so a tree's trunk and leaves are one mesh with two
	struct MeshSection
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		uint32_t MaterialSlot = 0;
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		// Three a triangle, counter-clockwise seen from its front
		std::vector<uint32_t> Indices;
		// One after the other, from the first index to the last; none: one section of every index,
		// in slot 0
		std::vector<MeshSection> Sections;
	};

	// Where a mesh's vertices are, in its own space
	struct MeshBounds
	{
		glm::vec3 Min{ 0.0f };
		glm::vec3 Max{ 0.0f };
	};

	// The mesh's sections as they are drawn: its own, or one of every index for a mesh without any
	std::vector<MeshSection> GetMeshSections(const MeshData& mesh);

	// What is wrong with the mesh, for a log; empty when nothing is. Its indices make whole triangles
	// and stay below its vertex count; its sections, if any, follow each other from the first index to
	// the last, each of whole triangles.
	std::string CheckMeshData(const MeshData& mesh);

	// Around every vertex; nothing (all zero) for a mesh without vertices
	MeshBounds ComputeMeshBounds(const MeshData& mesh);

	// Each vertex's tangent from the triangles around it: their direction of rising u, made
	// perpendicular to the normal, and the side of falling v (Lengyel). Exact on flat faces; on
	// curved ones the mean of the faces, as close as needed for the built-in meshes. Imported meshes
	// will take their file's tangents, or MikkTSpace's.
	void ComputeTangents(MeshData& mesh);
}

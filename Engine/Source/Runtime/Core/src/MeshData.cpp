#include "NyxPCH.h"
#include "MeshData.h"

#include <cmath>
#include <limits>

namespace Nyx
{
	std::vector<MeshSection> GetMeshSections(const MeshData& mesh)
	{
		if (!mesh.Sections.empty())
		{
			return mesh.Sections;
		}
		if (mesh.Indices.empty())
		{
			return {};
		}
		return { MeshSection{ .FirstIndex = 0, .IndexCount = static_cast<uint32_t>(mesh.Indices.size()), .MaterialSlot = 0 } };
	}

	std::string CheckMeshData(const MeshData& mesh)
	{
		if (mesh.Indices.size() % 3 != 0)
		{
			return std::to_string(mesh.Indices.size()) + " indices are no whole number of triangles";
		}
		if (mesh.Indices.size() > std::numeric_limits<uint32_t>::max() || mesh.Vertices.size() > std::numeric_limits<uint32_t>::max())
		{
			return "more vertices or indices than 32 bits count";
		}
		for (size_t index = 0; index < mesh.Indices.size(); ++index)
		{
			if (mesh.Indices[index] >= mesh.Vertices.size())
			{
				return "index " + std::to_string(index) + " is " + std::to_string(mesh.Indices[index]) + ", past the " +
					std::to_string(mesh.Vertices.size()) + " vertices";
			}
		}

		// One after the other from the first index to the last, so every triangle is drawn once
		uint32_t next = 0;
		for (size_t number = 0; number < mesh.Sections.size(); ++number)
		{
			const MeshSection& section = mesh.Sections[number];
			const std::string name = "section " + std::to_string(number);
			if (section.FirstIndex != next)
			{
				return name + " starts at index " + std::to_string(section.FirstIndex) + ", not where the one before ends (" + std::to_string(next) + ")";
			}
			if (section.IndexCount == 0 || section.IndexCount % 3 != 0)
			{
				return name + " has " + std::to_string(section.IndexCount) + " indices, no whole number of triangles";
			}
			if (static_cast<uint64_t>(section.FirstIndex) + section.IndexCount > mesh.Indices.size())
			{
				return name + " runs past the " + std::to_string(mesh.Indices.size()) + " indices";
			}
			next = section.FirstIndex + section.IndexCount;
		}
		if (!mesh.Sections.empty() && next != mesh.Indices.size())
		{
			return "the sections end at index " + std::to_string(next) + ", before the last of the " + std::to_string(mesh.Indices.size());
		}
		return {};
	}

	MeshBounds ComputeMeshBounds(const MeshData& mesh)
	{
		if (mesh.Vertices.empty())
		{
			return {};
		}
		MeshBounds bounds{ .Min = glm::vec3(std::numeric_limits<float>::max()), .Max = glm::vec3(std::numeric_limits<float>::lowest()) };
		for (const Vertex& vertex : mesh.Vertices)
		{
			bounds.Min = glm::min(bounds.Min, vertex.Position);
			bounds.Max = glm::max(bounds.Max, vertex.Position);
		}
		return bounds;
	}

	void ComputeTangents(MeshData& mesh)
	{
		std::vector<glm::vec3> alongU(mesh.Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> alongV(mesh.Vertices.size(), glm::vec3(0.0f));
		for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
		{
			const uint32_t corners[3] = { mesh.Indices[index], mesh.Indices[index + 1], mesh.Indices[index + 2] };
			const Vertex& a = mesh.Vertices[corners[0]];
			const Vertex& b = mesh.Vertices[corners[1]];
			const Vertex& c = mesh.Vertices[corners[2]];
			const glm::vec3 edge1 = b.Position - a.Position;
			const glm::vec3 edge2 = c.Position - a.Position;
			const glm::vec2 uv1 = b.UV - a.UV;
			const glm::vec2 uv2 = c.UV - a.UV;
			const float area = uv1.x * uv2.y - uv2.x * uv1.y;
			if (std::abs(area) < 1e-12f)
			{
				continue; // No texture on it, e.g. at a sphere's pole
			}
			const glm::vec3 u = (edge1 * uv2.y - edge2 * uv1.y) / area;
			const glm::vec3 v = (edge2 * uv1.x - edge1 * uv2.x) / area;
			for (const uint32_t corner : corners)
			{
				alongU[corner] += u;
				alongV[corner] += v;
			}
		}

		for (size_t index = 0; index < mesh.Vertices.size(); ++index)
		{
			Vertex& vertex = mesh.Vertices[index];
			const glm::vec3 n = glm::normalize(vertex.Normal);
			glm::vec3 t = alongU[index] - n * glm::dot(n, alongU[index]);
			if (glm::dot(t, t) < 1e-12f)
			{
				// No triangle with a texture: any direction along the surface
				t = glm::cross(std::abs(n.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f), n);
			}
			t = glm::normalize(t);
			// The bitangent points up the image, towards falling v: where a normal map's green points
			const float handedness = glm::dot(glm::cross(n, t), -alongV[index]) < 0.0f ? -1.0f : 1.0f;
			vertex.Tangent = glm::vec4(t, handedness);
		}
	}
}

#include "NyxPCH.h"
#include "RuntimeMeshComponent.h"

namespace Nyx
{
	std::vector<InspectedValue> DescribeRuntimeMesh(const RuntimeMeshComponent& component)
	{
		if (!component.Mesh)
		{
			return { { "Mesh", "none yet" } };
		}
		const MeshData& mesh = *component.Mesh;
		std::vector<InspectedValue> values = {
			{ "Mesh", std::to_string(mesh.Vertices.size()) + " vertices, " + std::to_string(mesh.Indices.size() / 3) + " triangles" }
		};
		const std::vector<MeshSection> sections = GetMeshSections(mesh);
		for (size_t index = 0; index < sections.size(); ++index)
		{
			values.push_back({ "Section " + std::to_string(index),
				std::to_string(sections[index].IndexCount / 3) + " triangles, material slot " + std::to_string(sections[index].MaterialSlot) });
		}
		if (const std::string problem = CheckMeshData(mesh); !problem.empty())
		{
			values.push_back({ "Not drawn", problem });
		}
		values.push_back({ "Revision", std::to_string(component.Revision) });
		return values;
	}
}

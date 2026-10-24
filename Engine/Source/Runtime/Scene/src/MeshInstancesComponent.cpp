#include "NyxPCH.h"
#include "MeshInstancesComponent.h"

namespace Nyx
{
	void SetMeshInstances(MeshInstancesComponent& component, std::shared_ptr<const MeshInstances> instances)
	{
		component.Instances = std::move(instances);
		++component.Revision;
	}

	std::vector<InspectedValue> DescribeMeshInstances(const MeshInstancesComponent& component)
	{
		if (!component.Instances)
		{
			return { { "Instances", "none yet" } };
		}
		const MeshInstances& instances = *component.Instances;
		const std::string cells = instances.Cells.empty() ? std::string("one cell") : std::to_string(instances.Cells.size()) + " cells";
		return { { "Instances", std::to_string(instances.Transforms.size()) + " in " + cells }, { "Revision", std::to_string(component.Revision) } };
	}
}

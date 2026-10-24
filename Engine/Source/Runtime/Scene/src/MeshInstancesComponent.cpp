#include "NyxPCH.h"
#include "MeshInstancesComponent.h"

namespace Nyx
{
	void SetMeshInstances(MeshInstancesComponent& component, std::shared_ptr<const MeshInstances> instances)
	{
		component.Instances = std::move(instances);
		++component.Revision;
	}
}

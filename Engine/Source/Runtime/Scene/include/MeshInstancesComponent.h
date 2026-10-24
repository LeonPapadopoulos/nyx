#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	// A cell of instances, for culling: bounds of the instances' origins in the entity's space
	// (the renderer adds the mesh's own bounds) and the range of Transforms it covers
	struct MeshInstanceCell
	{
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		uint32_t First = 0;
		uint32_t Count = 0;
	};

	struct MeshInstances
	{
		// Each relative to the entity's TransformComponent
		std::vector<glm::mat4> Transforms;
		// Optional: empty means one cell over all of them
		std::vector<MeshInstanceCell> Cells;
	};

	// Draws the entity's mesh (its MeshRendererComponent's mesh, or its RuntimeMeshComponent's)
	// with its materials once per transform instead of once. Run-time only, never saved, like
	// RuntimeMeshComponent: whoever fills it does so in a post-load step.
	struct MeshInstancesComponent
	{
		std::shared_ptr<const MeshInstances> Instances;
		uint64_t Revision = 0;
	};

	// Sets the instances and raises the revision, so the renderer uploads them again
	void SetMeshInstances(MeshInstancesComponent& component, std::shared_ptr<const MeshInstances> instances);
}

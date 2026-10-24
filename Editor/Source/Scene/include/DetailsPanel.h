#pragma once

#include <string>
#include <vector>

namespace Nyx
{
	struct MeshInstancesComponent;
	struct RuntimeMeshComponent;
}

namespace Nyx::Editor
{
	class SceneEditor;

	// A mesh made at run time as the details panel shows it, one line each: its vertices and
	// triangles, then each section's triangles and material slot
	std::vector<std::string> DescribeRuntimeMesh(const Nyx::RuntimeMeshComponent& component);
	// Mesh instances the same way: how many, in how many cells, and the revision
	std::vector<std::string> DescribeMeshInstances(const Nyx::MeshInstancesComponent& component);

	// The "Details" window: the selected entity's components with their reflected properties, Add
	// Component, and Remove Component on a component header's right-click menu. A mesh made at run
	// time and mesh instances, which aren't reflected, are shown read-only (DescribeRuntimeMesh(),
	// DescribeMeshInstances()).
	class DetailsPanel
	{
	public:
		void Draw(SceneEditor& editing);

		bool bOpen = true;

	private:
		void DrawWindow(SceneEditor& editing);
	};
}

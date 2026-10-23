#pragma once

#include "MeshData.h"

#include <cstdint>
#include <memory>

namespace Nyx
{
	// A mesh made by code while the world runs, e.g. a generated ground or a tree that grows: drawn,
	// picked and outlined in place of the entity's MeshRendererComponent's Mesh, each section with
	// the renderer's material of its slot (MeshRendererComponent::Material, ExtraMaterials). Runtime
	// only: not reflected, so never saved, edited or sent. Whoever made it, e.g. a generator
	// component's system, makes it again after a scene is loaded.
	//
	// Uploaded to the GPU once, and again only when Mesh points at other data or Revision changes:
	// after changing what Mesh holds, or giving it new data, raise Revision (MarkRuntimeMeshChanged()).
	// Entities may share one MeshData; the GPU keeps one copy of it.
	struct RuntimeMeshComponent
	{
		std::shared_ptr<const MeshData> Mesh;
		uint64_t Revision = 0;
	};

	// Gives the component new data and says so
	inline void SetRuntimeMesh(RuntimeMeshComponent& component, std::shared_ptr<const MeshData> mesh)
	{
		component.Mesh = std::move(mesh);
		++component.Revision;
	}

	// Says the data changed where it is
	inline void MarkRuntimeMeshChanged(RuntimeMeshComponent& component)
	{
		++component.Revision;
	}
}

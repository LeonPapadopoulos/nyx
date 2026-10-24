#pragma once

#include "Entity.h"
#include "MeshData.h"
#include "MeshInstancesComponent.h"
#include "ScatterComponent.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// At most this many instances a scatter, whatever its density: 64 MB of transforms
	constexpr uint32_t MaxScatterInstances = 1u << 20;

	// The scatter's instances on a surface mesh whose space goes into the scatter entity's by
	// surfaceToScatter. Its triangles are sampled by their area, density times the area of those that
	// reach into the scatter's area, each point with its own hashes of the seed and its number: one
	// is kept if it lies in the area, within the slope and height limits (against the normal
	// between the triangle's vertex normals, in the scatter's space) and at least Spacing from every
	// one kept before. Each is scaled, turned about its up and tilted to the normal as the values
	// say, and sunk along its up. Grouped into cells of CellSize over the area, the transforms of a
	// cell one after another. A function of the values and the mesh alone, the same on every
	// machine: integer hashes, and arithmetic that IEEE 754 rounds the same everywhere.
	MeshInstances ScatterOnMesh(const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter);

	// On the flat plane through the scatter entity, across its area: where a scatter without a
	// surface puts its instances
	MeshInstances ScatterOnPlane(const ScatterComponent& scatter);

	// A hash of every value the instances are made from, the surface's guid among them
	uint64_t HashScatterValues(const ScatterComponent& scatter);

	// The scatter's post-load step: gives `entity` the scatter's instances (MeshInstancesComponent)
	// and raises their revision, unless they were made from the same values on the same surface,
	// lying the same way. A surface that has no mesh yet, or none of this world, gives no instances,
	// until UpdateScatters() finds it has one.
	void UpdateScatter(ScatterComponent& scatter, Registry& world, Entity entity);

	// UpdateScatter() for every scatter of the world: each frame, in the editor and the game, so the
	// instances follow their surfaces, which other entities' steps change, in whatever order they
	// were loaded. Costs a guid lookup and a hash a scatter when nothing changed.
	void UpdateScatters(Registry& world);

	// Its remove step: the instances go with the component
	void RemoveScatter(Registry& world, Entity entity);
}

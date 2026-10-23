#pragma once

#include "Entity.h"
#include "GroundComponent.h"
#include "MeshData.h"

#include <cstdint>

namespace Nyx
{
	// The ground's height at x and z, in metres, around its entity: a function of the component's
	// values alone, and the same on every machine. It is made of additions, multiplications,
	// divisions, floor and square roots only, each correctly rounded by IEEE 754, in a fixed order:
	// no sine, power or exponent, whose results a C library may round its own way, and no random
	// distributions. Height is kept apart from the mesh, so a ground in chunks or of voxels later
	// can take its heights from here, and nothing else needs to know the ground is a heightfield.
	float SampleGroundHeight(const GroundComponent& ground, float x, float z);

	// The ground's colour where it has `height` and slopes `slopeDegrees`: grass, thinning to earth
	// up the height band, then earth and stone as it gets steeper
	glm::vec3 GetGroundColor(const GroundComponent& ground, float height, float slopeDegrees);

	// The ground's mesh: a grid over Size, at most 512 squares a side, as near Spacing apart as fits,
	// at the ground's heights; normals across the neighbouring heights, colours by GetGroundColor(),
	// UVs in metres (1 a metre, along X and Z), tangents along X. One section.
	MeshData MakeGroundMesh(const GroundComponent& ground);

	// A hash of every value the mesh is made from (GroundComponent::MadeFrom)
	uint64_t HashGroundValues(const GroundComponent& ground);

	// The ground's post-load step: gives `entity` the ground's mesh (RuntimeMeshComponent) and raises
	// its revision, unless the mesh there was made from the same values. Called in the editor and
	// the game when the component is loaded, added or edited.
	void UpdateGroundMesh(GroundComponent& ground, Registry& world, Entity entity);

	// Its remove step: the mesh goes with the component
	void RemoveGroundMesh(Registry& world, Entity entity);
}

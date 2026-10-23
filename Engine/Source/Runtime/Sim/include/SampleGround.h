#pragma once

#include "MeshData.h"
#include "SampleGroundComponent.h"
#include "SimSystem.h"

#include <cstdint>
#include <map>

namespace Nyx
{
	// The sample ground's height at x and z, around its entity: the bumps' sum, never further from 0
	// than BumpHeight
	float GetSampleGroundHeight(const SampleGroundComponent& ground, float x, float z);

	// Its mesh: a grid of Cells by Cells squares over Size, at its heights, with the slope's normals
	// and tangents, and UVs that repeat every 4 m. One section.
	MeshData MakeSampleGroundMesh(const SampleGroundComponent& ground);

	// Gives every entity with a SampleGroundComponent its mesh, as a generator does after a scene is
	// loaded: when it has none yet, and again when the values it was made from changed, e.g. by a live
	// edit. Nyx's game adds it to every world (GameLayer) until the ground generator takes its place.
	class SampleGroundSystem final : public ISimSystem
	{
	public:
		const char* GetName() const override { return "Sample Ground"; }
		void Step(const SimContext& context) override;

	private:
		// What each entity's mesh was made from, by entity
		std::map<uint64_t, SampleGroundComponent> Made;
	};
}

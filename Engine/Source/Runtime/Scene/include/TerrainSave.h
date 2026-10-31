#pragma once

#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	class BinaryReader;
	class BinaryWriter;

	// An SDF terrain's edits in their canonical form: the same bytes on every machine for the same
	// edits, to save, send and hash them. Little-endian, floats as their bits, in the order of the
	// revisions: the letters NTED, the version and how many, then each edit's revision, kind, shape,
	// mode, places, sizes, blend radius and material, and a bake's the layers it was made with
	// (HashTerrainLayers()), its corners' values and its materials, made on the spot if it has none.
	constexpr uint32_t TerrainEditsFormatVersion = 1;
	void WriteTerrainEdits(const SdfTerrainComponent& terrain, BinaryWriter& out);

	// The edits back, the bakes with their values. False, and nothing in `outEdits`, for edits not
	// whole, of a later version, or with an edit no terrain makes: a kind, shape or mode unknown, a
	// value not finite, revisions not counting up, a bake's brick off the bricks' multiples, its
	// materials neither one nor one a corner.
	bool ReadTerrainEdits(BinaryReader& in, std::vector<SdfTerrainEdit>& outEdits);

	// Each region of 256 m (its lowest corner over the size) that an edit comes into (where it can
	// dig or heap, its shape for paint, a bake's brick), with a hash of the seed and the layers and,
	// in order, the canonical bytes of those edits up to `revision`: to tell where two machines'
	// terrains differ. The same for the same layers and edits, whether bakes have their values and
	// whatever order chunks were made in. An edit over more than 4096 regions counts in every region
	// listed. In order of the regions, z slowest.
	constexpr float TerrainRegionSize = 256.0f;
	struct TerrainRegionHash
	{
		glm::ivec3 Region{ 0 };
		uint64_t Hash = 0;

		bool operator==(const TerrainRegionHash&) const = default;
	};
	std::vector<TerrainRegionHash> HashTerrainRegions(const SdfTerrainComponent& terrain, uint64_t revision = UINT64_MAX);
}

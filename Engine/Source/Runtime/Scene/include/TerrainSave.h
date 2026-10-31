#pragma once

#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
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
	// With `after`, only the edits after that revision: what a machine joining after a checkpoint
	// is sent.
	constexpr uint32_t TerrainEditsFormatVersion = 1;
	void WriteTerrainEdits(const SdfTerrainComponent& terrain, BinaryWriter& out, uint64_t after = 0);

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

	// The regions whose hashes differ between two lists, one listed in only one of them among them,
	// in order
	std::vector<glm::ivec3> FindTerrainRegionMismatches(std::span<const TerrainRegionHash> a, std::span<const TerrainRegionHash> b);

	// A checkpoint of a terrain's edits at its latest revision, for a save or a machine joining: the
	// letters NTCK, its version, the generator's fingerprint (GetTerrainGeneratorFingerprint()), the
	// seed and layers' hash (HashTerrainLayers()), the revision, the edits (WriteTerrainEdits()) and
	// their region hashes. A save is written whole or not at all: to a file beside it, then put in
	// its place.
	constexpr uint32_t TerrainCheckpointVersion = 1;
	void WriteTerrainCheckpoint(const SdfTerrainComponent& terrain, BinaryWriter& out);
	bool SaveTerrainCheckpoint(const SdfTerrainComponent& terrain, const std::filesystem::path& path);

	enum class ETerrainCheckpointRead : uint8_t
	{
		Read,
		// Not a checkpoint, not whole, or of a later version
		Unreadable,
		// Made with another generator, which would make other values of the same edits
		OtherGenerator,
		// Of a terrain with another seed or layers
		OtherLayers
	};
	struct TerrainCheckpointRead
	{
		ETerrainCheckpointRead Result = ETerrainCheckpointRead::Unreadable;
		uint64_t Revision = 0;
		// The regions whose hashes, made again from the edits read, aren't those it was written with:
		// a determinism bug, logged
		std::vector<glm::ivec3> Mismatched;
	};

	// Reads a checkpoint into the terrain's edits, its bakes with their values; the terrain's are left
	// as they were unless it is read.
	TerrainCheckpointRead ReadTerrainCheckpoint(BinaryReader& in, SdfTerrainComponent& terrain);
	TerrainCheckpointRead LoadTerrainCheckpoint(const std::filesystem::path& path, SdfTerrainComponent& terrain);

	// A joining machine takes the edits after its checkpoint as they come, each with its own revision
	// (from ReadTerrainEdits()): false, and nothing taken, unless they all come after its latest, in
	// order.
	bool TakeTerrainEdits(SdfTerrainComponent& terrain, std::span<const SdfTerrainEdit> edits);

	// Repair of a region whose hash differs from the host's: the host's bakes in the region, each its
	// revision and its canonical form with its values. The machine taking it uses the host's values
	// for its bakes of those revisions from then on; false, nothing changed, if one of them isn't the
	// same bake there (its edits differ: it joins again). It logs the region, the revision and the
	// fingerprint. A region with no bake gets the host's bricks baked first: those its edits come
	// into (BakeTerrainRegion()), the bakes going out as edits like any other.
	void WriteTerrainRegionRepair(const SdfTerrainComponent& host, const glm::ivec3& region, BinaryWriter& out);
	bool ApplyTerrainRegionRepair(SdfTerrainComponent& terrain, const glm::ivec3& region, BinaryReader& in);
	size_t BakeTerrainRegion(SdfTerrainComponent& host, const glm::ivec3& region);
}

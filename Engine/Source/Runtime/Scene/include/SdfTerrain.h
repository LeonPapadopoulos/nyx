#pragma once

#include "FieldMesher.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	// What an SDF terrain's field guarantees, and what it doesn't (Architecture.md, "Generated
	// content"):
	//
	// - The value at a point is negative inside, positive outside and 0 on the surface. Only its sign
	//   and where it is 0 are exact: it isn't the distance to the surface (y - height is no
	//   distance to a slope).
	// - It changes no faster than its slope bound, GetTerrainMaxSlope(): value / bound is a distance
	//   that is safe to move without crossing the surface. That is what skipping space and casting
	//   rays use. The bounds are proven from each layer's own, so they can be loose, never too small.
	// - Over a box, it lies within GetTerrainValueRange(), so a box whose range is all above 0 is all
	//   air and one all below is all solid.
	// - The same bits on every machine and in every build, one point at a time or many
	//   (SampleTerrainBatch()): only arithmetic IEEE 754 rounds the same everywhere, in a fixed
	//   order.
	//
	// Every point is in the terrain entity's space, in metres.

	// Where no layer made anything: air, this far from anything
	constexpr float SdfTerrainEmptyValue = 100000.0f;

	struct TerrainSample
	{
		float Value = SdfTerrainEmptyValue;
		// The material of the layer whose surface is nearest there (SdfTerrainLayer::Material)
		uint32_t Material = 0;
	};

	TerrainSample SampleTerrain(const SdfTerrainComponent& terrain, const glm::vec3& point);
	float SampleTerrainValue(const SdfTerrainComponent& terrain, const glm::vec3& point);

	// `count` points as three arrays into `outValues` and, if given, `outMaterials`: the same bits as
	// SampleTerrain() point by point, in batches of noise eight at a time where the processor can
	void SampleTerrainBatch(const SdfTerrainComponent& terrain, const float* x, const float* y, const float* z, size_t count, float* outValues,
		uint32_t* outMaterials = nullptr);

	// The terrain on a grid of `size` samples `cellSize` apart from `origin` (FieldGrid), values and
	// materials, in one batch: what a mesh of it is made from (MakeFieldMesh())
	// With `runParts`, its bricks' slabs in parts, the same bits.
	FieldGrid SampleTerrainGrid(const SdfTerrainComponent& terrain, const glm::vec3& origin, float cellSize, const glm::ivec3& size,
		const FieldRunParts& runParts = {});

	// The most the value can change a metre, in any direction; 0 for a terrain with no layers
	float GetTerrainMaxSlope(const SdfTerrainComponent& terrain);

	// The lowest and highest the value can be anywhere in the box: the tighter of what each layer's
	// reach allows (its heights, its noise's amplitude) and the value at the box's centre widened by
	// the slope bound times half the box's diagonal. Costs one sample.
	struct TerrainValueRange
	{
		float Lowest = SdfTerrainEmptyValue;
		float Highest = SdfTerrainEmptyValue;

		bool IsAllAir() const { return Lowest > 0.0f; }
		bool IsAllSolid() const { return Highest < 0.0f; }
	};
	TerrainValueRange GetTerrainValueRange(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax);

	// Which way is out at the point: the value's gradient, by central differences `step` metres
	// either side, made unit length; up where the value doesn't change
	glm::vec3 GetTerrainNormal(const SdfTerrainComponent& terrain, const glm::vec3& point, float step = 0.05f);

	struct TerrainRayHit
	{
		// Along the ray, in metres from its origin
		float Distance = 0.0f;
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		uint32_t Material = 0;
	};

	// Where a ray first meets the surface, within `maxDistance`: sphere tracing by the slope bound,
	// never less than 1 cm a step, then halving the last step to the crossing. A ray starting inside
	// hits at once. It can step past a wall thinner than a centimetre, never a thicker one.
	std::optional<TerrainRayHit> RayCastTerrain(const SdfTerrainComponent& terrain, const glm::vec3& origin, const glm::vec3& direction,
		float maxDistance);

	// Adds an edit after the others (SdfTerrainComponent::Edits), its revision the next one: returns
	// that revision. Only what makes the world's revisions (the editor's scene, a game's host) edits.
	uint64_t AppendTerrainEdit(SdfTerrainComponent& terrain, SdfTerrainEdit edit);

	// The latest edit's revision; 0 with none
	uint64_t GetTerrainRevision(const SdfTerrainComponent& terrain);

	// Baking: a brick's values kept as the edits so far made them, so sampling there no longer passes
	// the layers and those edits. A bake is an edit of its own (ESdfTerrainEditKind::Bake), after
	// those it was made from, numbered with them; it is undone with them (the edits stay), and every
	// machine makes the same of it. A brick is 8 cells of 1 m along each axis, its lowest corner on
	// whole multiples of 8 m.
	constexpr float SdfTerrainBakeBrickSize = 8.0f;
	constexpr int SdfTerrainBakeCorners = 9;

	// A bake's values (SdfTerrainEdit::Baked): the terrain's at the corners of its brick's cells, x
	// fastest, as the edits before it made them. In its brick, the terrain's value is then its cell's
	// corners' blended (trilinear), its material the nearest corner's, and the edits after the bake
	// apply over those. On the corners themselves, those are the same bits as without the bake, so
	// chunks of 1 m cells and coarser sample just the same; between them, they are off by how much
	// the terrain bends within a cell (finer chunks, a sharp vertex's crossings, the sky's rays), and
	// at a face onto a brick not baked the value can step by that much.
	struct SdfTerrainBakedBrick
	{
		static constexpr size_t Count = size_t(SdfTerrainBakeCorners) * SdfTerrainBakeCorners * SdfTerrainBakeCorners;
		std::array<float, Count> Values{};
		// One for each corner, or one for all when they are all the same
		std::vector<uint32_t> Materials;
		// The lowest and highest of the values in each eighth of the brick (its 5 x 5 x 5 corners), x
		// fastest
		std::array<float, 8> Lowest{};
		std::array<float, 8> Highest{};
		// The seed and layers they were made with (HashTerrainLayers()): with others, they are made again
		uint64_t Layers = 0;
	};

	// Bakes the brick at `brick` (its lowest corner over 8 m) after the terrain's edits, with the next
	// revision; returns it. Only what makes the world's revisions bakes (AppendTerrainEdit()).
	uint64_t AppendTerrainBake(SdfTerrainComponent& terrain, const glm::ivec3& brick);

	// Which bricks are due a bake: how many edits have come near each since its last bake (within a
	// brick of where it can turn the value's sign, of its shape for paint); an edit over more than
	// 64 bricks counts for none, as it is cheap to sample as it is. Brought up to the terrain's edits
	// as they are added, all of them the first time; when those it saw changed (an undo), from there on.
	class TerrainBakeCounts
	{
	public:
		void Update(const SdfTerrainComponent& terrain);

		// The bricks Bake After edits or more have come near, in order
		const std::set<uint64_t>& GetDue() const { return Due; }
		static glm::ivec3 BrickOf(uint64_t key);

	private:
		std::unordered_map<uint64_t, uint32_t> Near;
		std::set<uint64_t> Due;
		size_t Seen = 0;
		uint64_t SeenHash = 0;
		uint32_t Threshold = 0;
		bool bStarted = false;
	};

	// After the host's new edits: bakes each brick that the terrain's Bake After edits have come near
	// since its last bake, in order of their places. How many it baked.
	size_t BakeTerrainBricks(SdfTerrainComponent& terrain, TerrainBakeCounts& counts);

	// Gives the terrain's bakes that have no values (its edits loaded, or put back by an undo), or
	// values made with other layers, those of `earlier`'s where its layers and its edits up to there
	// are the same (CountSameTerrainEdits()), and makes at most `most` of the rest, in order, each
	// from the edits before it. How many it made. Until then, sampling makes what it needs of one as
	// it goes: the same bits, more slowly.
	size_t RestoreTerrainBakes(SdfTerrainComponent& terrain, const SdfTerrainComponent* earlier = nullptr, size_t most = SIZE_MAX);

	// A hash of the seed and the layers: what a bake's values are made from besides the edits
	uint64_t HashTerrainLayers(const SdfTerrainComponent& terrain);

	// The values of the bake at `index` among the terrain's edits: its own, or, if it has none or they
	// were made with other layers, made now from the edits before it (the same bits)
	std::shared_ptr<const SdfTerrainBakedBrick> GetTerrainBakeValues(const SdfTerrainComponent& terrain, size_t index);

	// A bake's values from its corners' values and materials (one for all, or one a corner; all the
	// same, one) and the layers they were made with, each eighth's range worked out
	std::shared_ptr<const SdfTerrainBakedBrick> MakeTerrainBakedBrick(const std::array<float, SdfTerrainBakedBrick::Count>& values, std::vector<uint32_t> materials,
		uint64_t layers);

	// The edits that can change a value somewhere in a box (by the interval arithmetic of the layers'
	// and the edits' ranges; one that can't is exactly no change there): how many, the latest of their
	// revisions (0 for none), and a hash of them all, which tells a box made with them from one made
	// with others, even of the same revisions (an undone edit's number made again)
	struct TerrainEditsReach
	{
		uint32_t Count = 0;
		uint64_t Revision = 0;
		uint64_t Hash = 0;
	};

	// Where a terrain's edits are, in cells of 32 m, to find those that can reach a box without going
	// through them all. Brought up to the terrain's edits after they change, those from the first
	// that changed taken again, so a new edit costs one.
	class TerrainEditIndex
	{
	public:
		// `same`: how many of the edits it has are still the terrain's (CountSameTerrainEdits())
		void Update(const SdfTerrainComponent& terrain, size_t same);

		// In order, the edits from `from` on whose shapes come into the box
		void Find(const glm::vec3& boxMin, const glm::vec3& boxMax, size_t from, std::vector<uint32_t>& out) const;

		size_t GetCount() const { return Bounds.size(); }
		// The largest blend radius among them, or that was
		float GetMaxBlendRadius() const { return MaxBlendRadius; }

	private:
		static constexpr float CellSize = 32.0f;
		// Edits that would fill more cells than this are looked at for every box
		static constexpr int MaxCells = 512;
		std::unordered_map<uint64_t, std::vector<uint32_t>> Cells;
		std::vector<uint32_t> Large;
		std::vector<std::pair<glm::ivec3, glm::ivec3>> Bounds;
		float MaxBlendRadius = 0.0f;
	};

	// How many of the terrain's edits, from the first, are those of `earlier`
	size_t CountSameTerrainEdits(const SdfTerrainComponent& terrain, const SdfTerrainComponent& earlier);

	// What is known of a box's values with the edits taken one after another: a range they are within,
	// by the layers and the edits seen so far, and those of the edits that reach it. Begun from the
	// layers (their range, tightened by their value at the centre: one sample), then continued over
	// the edits added after those seen, an edit at a time, so a box kept across edits costs one shape's
	// range an edit; with the terrain's index (TerrainEditIndex), only the edits near enough to reach
	// it are looked at, the same ones found. Rewound when edits it has seen change or go: back to
	// having seen only the first `count`, as it was after the last of them that reached it.
	struct TerrainBoxEdits
	{
		glm::vec3 BoxMin{ 0.0f };
		glm::vec3 BoxMax{ 0.0f };
		TerrainValueRange Range;
		// How many of the terrain's edits it has taken, and which of them reach it, with its range and
		// reach after each
		size_t Seen = 0;
		TerrainEditsReach Reach;
		std::vector<uint32_t> Reaching;
		std::vector<TerrainValueRange> RangesAfter;
		std::vector<TerrainEditsReach> ReachesAfter;
		// The layers' range, which it began from
		TerrainValueRange Layers;
	};
	TerrainBoxEdits BeginTerrainBoxEdits(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax);
	void ContinueTerrainBoxEdits(const SdfTerrainComponent& terrain, TerrainBoxEdits& box, const TerrainEditIndex* index = nullptr);
	void RewindTerrainBoxEdits(const SdfTerrainComponent& terrain, TerrainBoxEdits& box, size_t count);

	TerrainEditsReach GetTerrainEditsReaching(const SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax);

	// Leaves the terrain only those edits, and of a brick's bakes only the last, so sampling in the box
	// is the same bits with far fewer: what a chunk is made from
	void KeepTerrainEditsReaching(SdfTerrainComponent& terrain, const glm::vec3& boxMin, const glm::vec3& boxMax);

	// Whether an edit can change a value somewhere in the box that is between `lowest` and `highest`
	// (a value it can't change it leaves the same bits), and whether it can turn the value's sign
	// anywhere in the box whatever the values (a dig only within its blend radius and a quarter of it
	// of its shape, a heap likewise, paint never)
	bool CanTerrainEditChangeValues(const SdfTerrainEdit& edit, const glm::vec3& boxMin, const glm::vec3& boxMax, float lowest, float highest);
	bool CanTerrainEditTurnSign(const SdfTerrainEdit& edit, const glm::vec3& boxMin, const glm::vec3& boxMax);

	// A box outside which an edit can't turn the value's sign: its shape's, its blend radius and a
	// quarter of it wider; none for paint
	bool GetTerrainEditSignBounds(const SdfTerrainEdit& edit, glm::vec3& outMin, glm::vec3& outMax);

	// A box round where an edit does its work: where it can turn the sign, a paint's shape
	void GetTerrainEditBounds(const SdfTerrainEdit& edit, glm::vec3& outMin, glm::vec3& outMax);

	// Whether the terrain's edits are those of `earlier` (the same, in the same order) and maybe more
	// after them: what was known of boxes from those can be continued
	bool ExtendsTerrainEdits(const SdfTerrainComponent& terrain, const SdfTerrainComponent& earlier);

	// A hash of every edit, telling one set of edits from another; never 0
	uint64_t HashTerrainEdits(const SdfTerrainComponent& terrain);

	// A hash of each run of the edits from the first: `hashes[n]` of the first n, `hashes[0]` of
	// none. Carried on from those given, which must be the terrain's first ones (cleared when an
	// edit among them changed), so a new edit costs one.
	void HashTerrainEditRuns(const SdfTerrainComponent& terrain, std::vector<uint64_t>& hashes);

	// The nearest place a ball of `clearance` metres round `point` is out of the terrain, to push
	// something out of new terrain: the point itself if it is out already, else along the way out
	// (the normal, taken again each step) in steps of a tenth of the clearance, at least a centimetre
	// and at most 25 cm, within `maxDistance`. Out means the point and 26 points round it at the
	// clearance (the faces, edges and corners of a cube, made unit) all outside. Nothing if none is
	// found. A function of the terrain's values alone, usable where no chunk is made.
	std::optional<glm::vec3> FindTerrainFreePoint(const SdfTerrainComponent& terrain, const glm::vec3& point, float clearance, float maxDistance);
}

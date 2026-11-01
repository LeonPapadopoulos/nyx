#pragma once

#include "ComponentInspection.h"
#include "Entity.h"
#include "JobSystem.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Nyx
{
	class Registry;

	// An SDF terrain's occlusion volume: how solid the terrain is round the camera, in nested levels
	// of cells, each twice the size of the one before, so the renderer can dim the sky's light on
	// everything near it by how much of the sky the terrain hides (nyx.terrainocclusion): tunnels,
	// the bottoms of rifts, under overhangs and the underside; and shade what the sun reaches past
	// its shadow maps (nyx.shadows). Kept up to date while the world runs,
	// in the editor and the game, by UpdateTerrainChunks():
	// - Each level a window of Size cells a side round the first viewpoint, moving in steps of Step
	//   cells, its cells kept where they are as it moves (a cell at its world cell's index modulo
	//   Size), so a step fills only the slab it comes to.
	// - A cell's value is how solid the terrain is at its middle, from the terrain's value there over
	//   the cell's size: 0 air, 1 solid, between near the surface. The value isn't a distance, so
	//   this is a guess at how much of the cell is solid, good enough to dim light with.
	// - Filled on the job system's workers, a block of cells a job, blocks all air or all solid by the
	//   terrain's value range filled without sampling; what a fill made goes in all at once, when all
	//   of it is done, so the renderer never sees half of one. New values or a moved terrain fill it
	//   all again; new edits only the cells their bounds reach.
	// Run-time only, never saved.
	namespace TerrainOcclusion
	{
		// Cells a side of each level, and levels
		constexpr int Size = 64;
		constexpr int Levels = 6;
		// The finest level's cells, in metres; each level's are twice the one before's, so the
		// coarsest's window is 2 km across
		constexpr float FinestCell = 1.0f;
		// A window moves in steps of this many cells
		constexpr int Step = 8;
		// A block filled by one job, cells a side
		constexpr int Block = 16;
		// How many cells from its window's edge a level is read, no nearer: there a GPU's filter
		// would reach round to the cells at the other side
		constexpr float Margin = 2.0f;

		// The sky's visibility, as the shader works it out (nyx.terrainocclusion, the same numbers):
		// cones from a point off the surface, one along its normal and Cones - 1 round it tilted by
		// ConeTilt, each ConeTangent wide either side, marched in steps of StepWidths of their width
		// through the level whose cells are as wide, light lost to how solid each step is, out to
		// MaxDistance metres
		constexpr int Cones = 5;
		constexpr float ConeTilt = 0.96f;
		constexpr float ConeTangent = 0.58f;
		constexpr float StepWidths = 1.0f;
		constexpr float MaxDistance = 256.0f;
		constexpr int MaxSteps = 24;

		// The sun's light past its shadow maps, as the shader works it out: one cone towards the sun,
		// SunConeTangent wide either side (a little softer than the sun), from a cell and a half off
		// the surface in the finest level that holds the point, marched as the sky's cones are but
		// never finer than that level, out to SunMaxDistance metres
		constexpr float SunConeTangent = 0.04f;
		constexpr float SunMaxDistance = 1024.0f;
		constexpr int SunMaxSteps = 32;
		// The height fog within this many metres of the camera is lit as the camera is, by the sky and
		// the sun it sees past the terrain (nyx.atmosphere): in a tunnel or down a rift, dark
		constexpr float FogReach = 24.0f;
	}

	struct TerrainOcclusionLevel
	{
		float CellSize = 0.0f;
		// The window's first cell, in cells of this level from the world's origin
		glm::ivec3 Origin{ 0 };
		// Size cubed, how solid each cell is, 0 to 255: a cell at its world cell's index modulo Size,
		// x fastest
		std::vector<uint8_t> Solid;
		// Changed whenever its cells or window do
		uint64_t Revision = 0;

		glm::vec3 GetWindowMin() const { return glm::vec3(Origin) * CellSize; }
		glm::vec3 GetWindowMax() const { return glm::vec3(Origin + glm::ivec3(TerrainOcclusion::Size)) * CellSize; }
	};

	namespace Detail
	{
		struct TerrainOcclusionFill;
	}

	// On an SDF terrain entity with Sky Occlusion on, while the world runs
	struct TerrainOcclusionComponent
	{
		std::array<TerrainOcclusionLevel, TerrainOcclusion::Levels> Levels;
		// Every level filled once; until then the renderer leaves the sky's light alone
		bool bReady = false;
		// A fill is being made on the workers
		bool bFilling = false;
		// How dark it makes what it hides, 0 to 1 (SdfTerrainComponent::SkyOcclusion), and whether it
		// shades the sun's light past the shadow maps (SdfTerrainComponent::bFarShadows)
		float Strength = 1.0f;
		bool bFarShadows = true;
		// How many fills went in, the last's cells sampled and time
		uint32_t Fills = 0;
		uint64_t LastSampled = 0;
		double LastMilliseconds = 0.0;
		// The last fill, in flight or gone in
		std::shared_ptr<Detail::TerrainOcclusionFill> Fill;
	};

	// Keeps the occlusion volume of every SDF terrain with Sky Occlusion or Far Shadows on round the
	// first viewpoint; called by UpdateTerrainChunks()
	void UpdateTerrainOcclusion(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs);

	// How solid a level is at a point in the world, between its cells' middles as a GPU filters it;
	// below 0 outside its window or within Margin cells of its edge
	float SampleTerrainOcclusion(const TerrainOcclusionComponent& occlusion, int level, const glm::vec3& point);

	// How much of the sky a surface at `point` facing `normal` sees past the terrain, 0 to 1, dimmed
	// by the volume's Strength, as the renderer's shader works it out; 1 where the volume isn't ready
	float GetTerrainSkyVisibility(const TerrainOcclusionComponent& occlusion, const glm::vec3& point, const glm::vec3& normal);

	// How much of the sun's light reaches a surface at `point` facing `normal` past the terrain, 0 to 1,
	// `toSun` the way to the sun, as the renderer's shader works it out past the shadow maps; 1 where
	// the volume isn't ready, Far Shadows are off or the point is outside every window
	float GetTerrainSunVisibility(const TerrainOcclusionComponent& occlusion, const glm::vec3& point, const glm::vec3& normal, const glm::vec3& toSun);

	std::vector<InspectedValue> DescribeTerrainOcclusion(const TerrainOcclusionComponent& component);
}

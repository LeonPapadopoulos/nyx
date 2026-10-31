#pragma once

#include "GradientNoise.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	// A world plan worked out (SdfTerrainWorldPlan): what each place of the world was made to be, on a
	// grid of nodes 16 m apart, and its water as data. Made once from the terrain's seed and plan, the
	// same bits on every machine (only arithmetic IEEE 754 rounds the same everywhere, noise from
	// GradientNoise.h, every choice in a fixed order), and read by a Plan layer (SdfTerrain.h), which
	// blends each grid between its nodes.
	//
	// The landmass is a disc round the terrain's middle with a ragged edge, its top a base height
	// plus the plains' and rough land's noise, scaled by how much of each is there; its underside
	// hangs below by Thickness and ridges. Sea is ground below the sea's level, mostly by the edge.
	// Rifts are cut from the edge inland, rivers run from high ground down the plan's heights, lakes
	// are dug into the plains and filled to where they spill.
	struct TerrainWorldPlan
	{
		static constexpr float CellSize = 16.0f;
		// Nodes per side, the first at Origin, x fastest
		int Nodes = 0;
		glm::vec2 Origin{ 0.0f };

		// Each node's (Nodes * Nodes):
		// The top's base height, before noise
		std::vector<float> Height;
		// How far the plains' noise and rough land's crests lift and lower the top there, in metres
		std::vector<float> PlainsAmplitude;
		std::vector<float> RoughAmplitude;
		// How far it is to the nearest rift's middle, less the rift's half width at its floor, in
		// metres: negative inside; at most MaxReach
		std::vector<float> Rift;
		// The same for the nearest river's bed, and the height of that bed
		std::vector<float> Channel;
		std::vector<float> ChannelBed;
		// How open tunnels are there, 0 (none) to 1 (TunnelWidth wide)
		std::vector<float> Tunnels;
		// Which kind of ground is most there (ESdfTerrainLandform); NotLand off the landmass
		std::vector<uint8_t> Landform;
		static constexpr uint8_t NotLand = 255;

		// Distances in Rift and Channel past this are cut to it
		static constexpr float MaxReach = 96.0f;

		// The edge: the distance from the middle less Radius and EdgeRoughness times this noise
		float Radius = 0.0f;
		FractalNoise3D EdgeNoise;

		struct River
		{
			// From its source down, through any lakes on its way
			std::vector<glm::vec2> Points;
			// Its bed's height at each point, never rising downstream
			std::vector<float> Beds;
			// Where it ends: in the sea, over the landmass's edge, into a rift, or in another river
			enum class EEnd : uint8_t
			{
				Sea,
				Edge,
				Rift,
				River
			} End = EEnd::Sea;
		};
		std::vector<River> Rivers;

		struct Lake
		{
			glm::vec2 Center{ 0.0f };
			// Where it spills over its basin's rim
			float Level = 0.0f;
		};
		std::vector<Lake> Lakes;

		// The seed and plan it was made from
		uint32_t Seed = 0;
		SdfTerrainWorldPlan Made;

		// Over a block of 8 x 8 nodes (from node 8i, 8j to 8i + 8, 8j + 8 inclusive), the lowest and
		// highest of each grid, for the bounds of a Plan layer over a box (GetRange())
		struct Bounds
		{
			float LowHeight = 0.0f;
			float HighHeight = 0.0f;
			float HighPlains = 0.0f;
			float HighRough = 0.0f;
			float LowRift = 0.0f;
			float HighRift = 0.0f;
			float LowChannel = 0.0f;
			float HighChannel = 0.0f;
			float LowBed = 0.0f;
			float HighBed = 0.0f;
			float HighTunnels = 0.0f;
		};
		static constexpr int BlockNodes = 8;
		int Blocks = 0;
		std::vector<Bounds> BlockBounds;

		// How fast each grid, blended between nodes, changes a metre at most, along the ground
		float HeightSlope = 0.0f;
		float PlainsSlope = 0.0f;
		float RoughSlope = 0.0f;
		float RiftSlope = 0.0f;
		float ChannelSlope = 0.0f;
		float BedSlope = 0.0f;
		float TunnelsSlope = 0.0f;

		// The most of each amplitude anywhere, and whether there are tunnels anywhere
		float MaxPlainsAmplitude = 0.0f;
		float MaxRoughAmplitude = 0.0f;
		bool bTunnels = false;

		// A grid blended between its four nodes round the point (along X and Z); its edge nodes past the
		// grid
		float At(const std::vector<float>& grid, float x, float z) const;

		// The nodes' bounds over the blocks a box (along X and Z) meets
		Bounds GetRange(const glm::vec2& boxMin, const glm::vec2& boxMax) const;

		// The landmass's edge: negative inside, about the distance to it, in metres
		float EdgeValue(float x, float z) const;

		// The node nearest a point, clamped to the grid
		glm::ivec2 NodeOf(const glm::vec2& point) const;
		glm::vec2 PointOf(int i, int j) const { return Origin + glm::vec2(float(i), float(j)) * CellSize; }
		size_t IndexOf(int i, int j) const { return size_t(i) + size_t(j) * size_t(Nodes); }
	};

	// Works the plan out; at 2 km a few milliseconds, at 20 km about a second
	std::shared_ptr<const TerrainWorldPlan> MakeTerrainWorldPlan(uint32_t seed, const SdfTerrainWorldPlan& plan);

	// Whether two plans' values are all the same
	bool IsSameTerrainWorldPlan(const SdfTerrainWorldPlan& a, const SdfTerrainWorldPlan& b);

	// A hash of a plan's values, for what its ground is made from
	uint64_t HashTerrainWorldPlan(const SdfTerrainWorldPlan& plan);

	// The plan the terrain's seed and plan make: its own (SdfTerrainComponent::Planned) when that is
	// up to date, else one kept for the process, made the first time it is asked for. Safe from any
	// thread.
	std::shared_ptr<const TerrainWorldPlan> GetTerrainWorldPlan(const SdfTerrainComponent& terrain);

	// Brings the terrain's own plan up to date, so sampling finds it at once; what owns the terrain
	// calls it before its jobs read it (UpdateTerrainChunks(), the collision's sync). True if it was
	// made again.
	bool PrepareTerrainWorldPlan(SdfTerrainComponent& terrain);

	// Whether any of the terrain's enabled layers is a Plan layer
	bool UsesTerrainWorldPlan(const SdfTerrainComponent& terrain);
}

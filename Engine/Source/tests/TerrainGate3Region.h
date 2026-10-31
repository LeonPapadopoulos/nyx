#pragma once

// Gate 3's test region: a world plan 2 km wide made into ground, with the plan's defaults (plains
// and rough land, sea by the edge, two rifts, three rivers, lakes, tunnels), fine detail over every
// surface, and flat colours by rules: grass on the plains, darker on rough land, dirt on slopes,
// stone on cliffs and deep in rifts and tunnels, darker under overhangs and the underside, sand
// along the shore and silt under the sea

#include "SdfTerrainComponent.h"

namespace Nyx::TerrainGate3
{
	// The region's material numbers
	enum ERegionMaterial : uint32_t
	{
		Grass,
		Dirt,
		Stone,
		Sand,
		Silt,
		RoughGrass,
		DarkStone
	};

	inline SdfTerrainComponent MakeRegion()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 2;
		terrain.ViewDistance = 4096.0f;
		terrain.Layers.clear();

		SdfTerrainLayer ground;
		ground.Kind = ESdfTerrainLayerKind::Plan;
		ground.Frequency = 0.006f;
		ground.Octaves = 5;
		ground.Material = Grass;
		terrain.Layers.push_back(ground);

		SdfTerrainLayer detail;
		detail.Kind = ESdfTerrainLayerKind::Detail;
		detail.Amplitude = 0.4f;
		detail.Frequency = 0.25f;
		detail.Octaves = 2;
		terrain.Layers.push_back(detail);

		terrain.MaterialColors = { glm::vec3(0.16f, 0.34f, 0.07f), glm::vec3(0.30f, 0.20f, 0.10f), glm::vec3(0.33f, 0.32f, 0.30f), glm::vec3(0.62f, 0.55f, 0.38f),
			glm::vec3(0.26f, 0.25f, 0.19f), glm::vec3(0.22f, 0.28f, 0.10f), glm::vec3(0.16f, 0.15f, 0.14f) };
		const float sea = terrain.Plan.SeaLevel;
		auto rule = [&](uint32_t material) -> SdfTerrainMaterialRule&
		{
			SdfTerrainMaterialRule& added = terrain.MaterialRules.emplace_back();
			added.Over = Grass;
			added.Material = material;
			return added;
		};
		rule(DarkStone).MinSlope = 110.0f;
		rule(Stone).MinDepth = 12.0f;
		rule(Stone).MinSlope = 42.0f;
		rule(Silt).MaxHeight = sea - 1.0f;
		rule(Sand).MaxHeight = sea + 2.5f;
		rule(Dirt).MinSlope = 28.0f;
		SdfTerrainMaterialRule& rough = rule(RoughGrass);
		rough.bOnLandform = true;
		rough.Landform = ESdfTerrainLandform::Roughland;
		return terrain;
	}
}

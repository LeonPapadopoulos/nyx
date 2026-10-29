#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	// What a layer of an SDF terrain makes. Saved as its number: new kinds go at the end.
	NYX_ENUM()
	enum class ESdfTerrainLayerKind : uint8_t
	{
		// Ground at a height, with noise for hills and terraces: solid below, air above
		Heightfield,
		// A floating mass of ground: a top, an underside hanging below it, and a ragged edge round its
		// centre
		Landmass,
		// Winding tunnels where two noises are both near 0, within a band of heights
		Tunnels,
		// Open chambers where a noise is high, within a band of heights
		Chambers,
		// Small bumps over every surface the layers before made: their surfaces pushed in and out
		Detail
	};

	// How a layer's shape joins what the layers before it made. Saved as its number.
	NYX_ENUM()
	enum class ESdfTerrainCombine : uint8_t
	{
		// Solid where the layer or what came before is
		Add,
		// Air where the layer is solid: it cuts its shape out of what came before
		Carve,
		// Solid only where both are
		Intersect
	};

	// One layer of an SDF terrain. Every layer has the same values; each kind reads the ones its
	// tooltips name.
	NYX_REFLECT()
	struct SdfTerrainLayer
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Off: as if it weren't there"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Heightfield: ground at a height; Landmass: a floating mass with an underside; Tunnels and Chambers: caves; Detail: bumps on every surface before it"))
		ESdfTerrainLayerKind Kind = ESdfTerrainLayerKind::Heightfield;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Add: solid where it is; Carve: cut out of what came before (caves usually); Intersect: solid only where both are. Detail ignores it"))
		ESdfTerrainCombine Combine = ESdfTerrainCombine::Add;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Blend Radius", DragSpeed = 0.1, Tooltip = "Over how many metres it blends into what came before, rounding the corner where they meet; 0: a sharp edge"))
		float BlendRadius = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The material number of the surfaces it makes. Carve and Detail keep the material of what they cut or push"))
		uint32_t Material = 0;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 1.0, Tooltip = "In metres. Heightfield: the ground's height; Landmass: its top's; Tunnels and Chambers: the top of their band"))
		float Height = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.5, Tooltip = "In metres. Landmass: from its top down to its underside; Tunnels and Chambers: how deep their band reaches below Height"))
		float Depth = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.1, Tooltip = "In metres. Heightfield: how far the noise lifts and lowers the ground; Landmass: its top's roughness; Detail: how far surfaces are pushed in and out"))
		float Amplitude = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 1.0, Tooltip = "Landmass: its centre along X and Z, in metres"))
		glm::vec2 Center{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.5, Tooltip = "In metres. Landmass: from its centre to its edge; Tunnels: about how wide they are"))
		float Radius = 200.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Underside Depth", Category = "Shape", DragSpeed = 0.5, Tooltip = "Landmass: how far, in metres, its underside hangs below Depth in ridges"))
		float UndersideDepth = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Edge Roughness", Category = "Shape", DragSpeed = 0.5, Tooltip = "Landmass: how far, in metres, its edge goes in and out round its radius"))
		float EdgeRoughness = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.1, Tooltip = "Landmass: over how many metres its rim is rounded where top and edge meet; 0: a sharp rim"))
		float Rounding = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.01, Tooltip = "Chambers: how much of the band is open, 0 to 1: the noise is open above 1 minus this"))
		float Openness = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Terrace Height", Category = "Terraces", DragSpeed = 0.05, Tooltip = "Heightfield and Landmass top: the height of each step, in metres; 0: none"))
		float TerraceHeight = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Terrace Sharpness", Category = "Terraces", DragSpeed = 0.01, Tooltip = "0: soft steps; up to 0.9: flat steps with steep faces between them"))
		float TerraceSharpness = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", Tooltip = "Another seed, another shape of the same kind; added to the terrain's seed"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", Tooltip = "Layers of noise, each finer and fainter than the one before; 1 to 16"))
		uint32_t Octaves = 5;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.0005, Tooltip = "The first layer's bumps a metre: 0.005 is one every 200 m"))
		float Frequency = 0.005f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.01, Tooltip = "How much finer each layer of noise is than the one before"))
		float Lacunarity = 2.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.01, Tooltip = "How much fainter each layer of noise is than the one before"))
		float Gain = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", Tooltip = "Ridged noise: sharp crests instead of round hills (Heightfield, Landmass top, Chambers)"))
		bool bRidged = false;
	};

	// How an SDF terrain's chunks are meshed (FieldMesher.h). Saved as its number.
	NYX_ENUM()
	enum class ESdfTerrainMesher : uint8_t
	{
		// A vertex in each cell the surface crosses; sharp where its material is in Sharp Materials
		Dual,
		// Vertices on the grid's edges, every one smooth
		MarchingCubes
	};

	// Terrain as a signed field over a volume, so it can overhang, hollow out and float: a stack of
	// layers, each joined to what the ones before it made, evaluated in order. Its value at a point
	// (SampleTerrain(), SdfTerrain.h) is a function of these values alone, the same on every machine:
	// negative inside, positive outside, 0 on the surface. Heights are along the entity's Y, around
	// it. Meshed in chunks round the camera, in the editor and the game (TerrainChunks.h), and drawn
	// by the entity's Mesh Renderer.
	NYX_REFLECT(Component, meta = (DisplayName = "SDF Terrain"))
	struct SdfTerrainComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Terrain", Tooltip = "Another seed, another terrain of the same kind: every layer's noise changes"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Terrain", Tooltip = "Evaluated in order, each joined to what the ones before it made"))
		std::vector<SdfTerrainLayer> Layers{ SdfTerrainLayer{} };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "View Distance", Category = "Chunks", DragSpeed = 4.0, Tooltip = "How far from the camera, in metres, chunks of 32 m are made and drawn"))
		float ViewDistance = 256.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Chunks", Tooltip = "Dual: a vertex in each cell, sharp edges for the Sharp Materials; Marching Cubes: vertices on the grid's edges, all smooth"))
		ESdfTerrainMesher Mesher = ESdfTerrainMesher::Dual;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sharp Materials", Category = "Chunks", Tooltip = "The material numbers whose surfaces keep sharp edges and corners, such as cliffs (Dual only)"))
		std::vector<uint32_t> SharpMaterials;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Material Colours", Category = "Chunks", UI = Color, Tooltip = "Each material number's vertex colour, linear RGB, from 0; a number past the list is white"))
		std::vector<glm::vec3> MaterialColors{ glm::vec3(0.13f, 0.30f, 0.05f), glm::vec3(0.22f, 0.14f, 0.07f), glm::vec3(0.26f, 0.25f, 0.23f) };
	};
}

#include "Generated/Runtime/SdfTerrainComponent.reflect.h"

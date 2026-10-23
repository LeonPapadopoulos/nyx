#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// What lifts the ground towards its edges, so it lies in a basin
	NYX_ENUM()
	enum class EGroundBasin : uint8_t
	{
		None,
		// Rises towards the edges of X, as a valley running along Z
		Valley,
		// Rises towards every edge, round the middle
		Bowl
	};

	// A ground made from its values: noise, a basin, a flat place in the middle and terraces, coloured
	// by slope and height. Its height is a function of these values alone (SampleGroundHeight(),
	// GroundMesh.h); its mesh is made from that by its post-load step, in the editor and in the game,
	// whenever the component is loaded, added or edited, and is never saved (RuntimeMeshComponent).
	// A Mesh Renderer with a material that shows vertex colours (Materials/VertexColor.nyxmat) draws
	// it, a Mesh Collider makes it ground to walk on. Heights are around the entity, along its Y.
	NYX_REFLECT(Component, meta = (DisplayName = "Ground"))
	struct GroundComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", DragSpeed = 1.0, Tooltip = "Its width along X and depth along Z, in metres, around the entity"))
		glm::vec2 Size{ 128.0f, 128.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", DragSpeed = 0.05, Tooltip = "Metres between the mesh's vertices; at most 512 squares a side"))
		float Spacing = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "Another seed, another ground of the same kind"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", Tooltip = "Layers of noise, each finer and fainter than the one before; 1 to 12"))
		uint32_t Octaves = 5;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.001, Tooltip = "The first layer's hills a metre: 0.02 is one every 50 m"))
		float Frequency = 0.02f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.05, Tooltip = "The noise's highest rise and deepest dip, in metres"))
		float Amplitude = 3.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.01, Tooltip = "How much finer each layer is than the one before"))
		float Lacunarity = 2.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Noise", DragSpeed = 0.01, Tooltip = "How much fainter each layer is than the one before"))
		float Gain = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", Tooltip = "What lifts the ground towards its edges"))
		EGroundBasin Basin = EGroundBasin::Valley;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Basin Depth", Category = "Shape", DragSpeed = 0.1, Tooltip = "How high the basin rises at the edges, in metres"))
		float BasinDepth = 14.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Basin Floor", Category = "Shape", DragSpeed = 0.01, Tooltip = "How much of the way to the edges the basin's floor stays low, 0 to 1"))
		float BasinFloor = 0.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Flat Radius", Category = "Shape", DragSpeed = 0.1, Tooltip = "Around the middle, in metres, the ground is nearly flat, to play on"))
		float FlatRadius = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Flat Blend", Category = "Shape", DragSpeed = 0.1, Tooltip = "Over how many metres past the flat place the noise comes back"))
		float FlatBlend = 12.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Terrace Height", Category = "Shape", DragSpeed = 0.05, Tooltip = "The height of each terrace, in metres; 0: none"))
		float TerraceHeight = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Terrace Sharpness", Category = "Shape", DragSpeed = 0.01, Tooltip = "0: gentle slopes between terraces, 1: steps"))
		float TerraceSharpness = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Grass", Category = "Colours", UI = Color, Tooltip = "Linear RGB"))
		glm::vec3 GrassColor{ 0.13f, 0.30f, 0.05f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Earth", Category = "Colours", UI = Color, Tooltip = "Linear RGB"))
		glm::vec3 EarthColor{ 0.22f, 0.14f, 0.07f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Stone", Category = "Colours", UI = Color, Tooltip = "Linear RGB"))
		glm::vec3 StoneColor{ 0.26f, 0.25f, 0.23f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Earth Slope", Category = "Colours", DragSpeed = 0.1, Tooltip = "Steeper than this, in degrees, grass gives way to earth"))
		float EarthSlopeDegrees = 24.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Stone Slope", Category = "Colours", DragSpeed = 0.1, Tooltip = "Steeper than this, in degrees, earth gives way to stone"))
		float StoneSlopeDegrees = 36.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Slope Blend", Category = "Colours", DragSpeed = 0.1, Tooltip = "Over how many degrees around each slope the colours blend"))
		float SlopeBlendDegrees = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Grass Thins From", Category = "Colours", DragSpeed = 0.1, Tooltip = "Above this height, in metres, grass starts to thin to earth"))
		float GrassThinStart = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Grass Gone At", Category = "Colours", DragSpeed = 0.1, Tooltip = "Above this height, in metres, there is no more grass"))
		float GrassThinEnd = 16.0f;

		// What the entity's mesh was made from (HashGroundValues()), so a post-load step that runs
		// for another component of the entity, or with nothing changed, makes nothing again. Not
		// reflected: a copy or a restored component has 0 and is made again.
		uint64_t MadeFrom = 0;
	};
}

#include "Generated/Runtime/GroundComponent.reflect.h"

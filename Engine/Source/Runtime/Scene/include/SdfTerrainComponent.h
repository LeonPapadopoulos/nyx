#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
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
		Detail,
		// A box at Position, Half Size each way along its own axes, turned by Rotation, its edges
		// rounded by Rounding
		Box,
		// A ball of Radius round Position
		Sphere,
		// A rod of Radius from Position to End, round at both ends
		Capsule
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Heightfield: ground at a height; Landmass: a floating mass with an underside; Tunnels and Chambers: caves; Detail: bumps on every surface before it; Box, Sphere and Capsule: shapes placed where they are"))
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.5, Tooltip = "In metres. Landmass: from its centre to its edge; Tunnels: about how wide they are; Sphere and Capsule: their radius"))
		float Radius = 200.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Underside Depth", Category = "Shape", DragSpeed = 0.5, Tooltip = "Landmass: how far, in metres, its underside hangs below Depth in ridges"))
		float UndersideDepth = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Edge Roughness", Category = "Shape", DragSpeed = 0.5, Tooltip = "Landmass: how far, in metres, its edge goes in and out round its radius"))
		float EdgeRoughness = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.1, Tooltip = "Landmass: over how many metres its rim is rounded where top and edge meet; Box: how round its edges are; 0: sharp"))
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Placed", DragSpeed = 0.1, Tooltip = "In metres, in the terrain's space. Box and Sphere: their middle; Capsule: one end"))
		glm::vec3 Position{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Half Size", Category = "Placed", DragSpeed = 0.05, Tooltip = "Box: half its size along each of its axes, in metres"))
		glm::vec3 HalfSize{ 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Placed", DragSpeed = 0.1, Tooltip = "Capsule: its other end, in metres, in the terrain's space"))
		glm::vec3 End{ 0.0f, 2.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Placed", DragSpeed = 0.5, Tooltip = "Box: how it is turned, in degrees about X, then Y, then Z"))
		glm::vec3 Rotation{ 0.0f };
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

	// What an SDF terrain's chunks show in place of their materials' colours, to see how they are
	// made. Saved as its number.
	NYX_ENUM()
	enum class ESdfTerrainDebugView : uint8_t
	{
		// Their materials' colours
		None,
		// A colour for each level of detail
		Levels,
		// Each level's colour, darker, and the triangles that join a chunk to one of another level
		// bright
		Seams
	};

	// The shape of an edit to an SDF terrain. Saved as its number: new shapes go at the end.
	NYX_ENUM()
	enum class ESdfTerrainEditShape : uint8_t
	{
		// A ball of Radius round Position
		Sphere,
		// A rod of Radius from Position to End, round at both ends
		Capsule,
		// A box at Position, Half Size each way along its own axes, turned by Rotation
		Box
	};

	// What an edit does. Saved as its number: new modes go at the end.
	NYX_ENUM()
	enum class ESdfTerrainEditMode : uint8_t
	{
		// Cuts its shape out: a dig
		Subtract,
		// Fills its shape in, of its Material
		Add,
		// Gives the terrain in its shape its Material, its surface left as it is
		Paint
	};

	// What an entry of an SDF terrain's edits is. Saved as its number: new kinds go at the end.
	NYX_ENUM()
	enum class ESdfTerrainEditKind : uint8_t
	{
		// A shape and what it does to the terrain
		Shape,
		// The terrain's values in a brick, as the edits before it made them, in place of those edits and
		// the layers there (BakeTerrainBricks(), SdfTerrain.h)
		Bake
	};

	struct SdfTerrainBakedBrick;

	// One edit of an SDF terrain: a shape and what it does, numbered by the revision it made, or a
	// bake. Edits apply after every layer, in the order of their revisions (AppendTerrainEdit(),
	// SdfTerrain.h).
	NYX_REFLECT()
	struct SdfTerrainEdit
	{
		// The terrain's revision this edit made: 1 for the first, one more for each after it
		NYX_PROPERTY(Serialize)
		uint64_t Revision = 0;

		NYX_PROPERTY(Serialize)
		ESdfTerrainEditKind Kind = ESdfTerrainEditKind::Shape;

		NYX_PROPERTY(Serialize)
		ESdfTerrainEditShape Shape = ESdfTerrainEditShape::Sphere;

		NYX_PROPERTY(Serialize)
		ESdfTerrainEditMode Mode = ESdfTerrainEditMode::Subtract;

		// In the terrain's space, in metres: a sphere's or box's middle, a capsule's one end; a bake's
		// brick's lowest corner
		NYX_PROPERTY(Serialize)
		glm::vec3 Position{ 0.0f };

		// A capsule's other end
		NYX_PROPERTY(Serialize)
		glm::vec3 End{ 0.0f };

		// A box's half size along each of its axes, and how it is turned, in degrees about X, then Y,
		// then Z
		NYX_PROPERTY(Serialize)
		glm::vec3 HalfSize{ 1.0f };

		NYX_PROPERTY(Serialize)
		glm::vec3 Rotation{ 0.0f };

		// A sphere's and a capsule's
		NYX_PROPERTY(Serialize)
		float Radius = 1.0f;

		// Over how many metres it blends into the terrain, rounding the edge it makes; 0: sharp
		NYX_PROPERTY(Serialize)
		float BlendRadius = 0.0f;

		// Add and Paint: the material number it gives
		NYX_PROPERTY(Serialize)
		uint32_t Material = 0;

		// A bake's values, made from the edits before it: run-time only, never saved, made again from
		// those where missing (RestoreTerrainBakes(), SdfTerrain.h)
		std::shared_ptr<const SdfTerrainBakedBrick> Baked;
	};

	// A kind of ground a world plan places (SdfTerrainWorldPlan). Saved as its number: new kinds go at
	// the end.
	NYX_ENUM()
	enum class ESdfTerrainLandform : uint8_t
	{
		// Low, gentle ground
		Plains,
		// Higher, broken ground with crests
		Roughland,
		// A deep cut from the landmass's edge inland
		Rift,
		// Ground below the sea's level
		Sea,
		// Tunnels through the ground
		Tunnels
	};

	// A place a world plan is steered to make one kind of ground: the seed still shapes it, round it
	NYX_REFLECT()
	struct SdfTerrainPlanPin
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Plains, Roughland, Sea and Tunnels: more of that kind round it; Rift: a rift passes it"))
		ESdfTerrainLandform Landform = ESdfTerrainLandform::Plains;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 4.0, Tooltip = "Along X and Z, in metres, in the terrain's space"))
		glm::vec2 Position{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 4.0, Tooltip = "How far round it, in metres, it steers"))
		float Radius = 250.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.05, Tooltip = "How strongly: 1 nearly always wins at its middle, 0 does nothing"))
		float Strength = 1.0f;
	};

	// What a world is made of, in what proportions, worked out once from the terrain's seed
	// (MakeTerrainWorldPlan(), TerrainWorldPlan.h) on a grid of 16 m: a floating landmass of plains and rough land with sea on it, rifts cut from its edge
	// inland, rivers running down to the sea, a lake, a rift or over the edge, lakes, and tunnels.
	// Never saved itself: the same seed and these values make the same plan on every machine.
	NYX_REFLECT()
	struct SdfTerrainWorldPlan
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "World", DragSpeed = 8.0, Tooltip = "How wide the world is, in metres: the landmass lies within a square this wide round the terrain's middle"))
		float Size = 2048.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Region Size", Category = "World", DragSpeed = 4.0, Tooltip = "About how wide, in metres, a stretch of one kind of ground is"))
		float RegionSize = 450.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "World", DragSpeed = 1.0, Tooltip = "The plains' height, in metres"))
		float Height = 20.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "World", DragSpeed = 1.0, Tooltip = "How thick the landmass is, in metres, from its top down to where its underside starts hanging"))
		float Thickness = 140.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Underside Depth", Category = "World", DragSpeed = 1.0, Tooltip = "How far, in metres, its underside hangs below that in ridges"))
		float UndersideDepth = 90.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Edge Roughness", Category = "World", DragSpeed = 1.0, Tooltip = "How far, in metres, its edge goes in and out"))
		float EdgeRoughness = 160.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "World", DragSpeed = 0.1, Tooltip = "Over how many metres its rim is rounded where the top meets the edge"))
		float Rounding = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sea", DragSpeed = 0.01, Tooltip = "How much of the landmass is under the sea, 0 to 0.8; mostly by its edge, where it pours off"))
		float Sea = 0.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sea Level", Category = "Sea", DragSpeed = 0.5, Tooltip = "The sea's height, in metres"))
		float SeaLevel = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sea Depth", Category = "Sea", DragSpeed = 0.5, Tooltip = "How deep, in metres, the sea's floor lies below its level, away from the shore"))
		float SeaDepth = 25.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rough Land", DragSpeed = 0.01, Tooltip = "How much of the land above the sea is rough, 0 to 1; the rest is plains"))
		float Roughland = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rough Lift", Category = "Rough Land", DragSpeed = 0.5, Tooltip = "How much higher, in metres, rough land stands than the plains"))
		float RoughLift = 35.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rough Height", Category = "Rough Land", DragSpeed = 0.5, Tooltip = "How far, in metres, rough land's crests rise and its hollows fall"))
		float RoughHeight = 50.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Plains Height", Category = "Rough Land", DragSpeed = 0.1, Tooltip = "How far, in metres, the plains rise and fall"))
		float PlainsHeight = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rifts", Tooltip = "How many rifts are cut from the edge inland, besides those Rift pins ask for"))
		uint32_t Rifts = 2;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rift Length", Category = "Rifts", DragSpeed = 4.0, Tooltip = "How far inland, in metres, a rift reaches"))
		float RiftLength = 800.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rift Width", Category = "Rifts", DragSpeed = 0.5, Tooltip = "How wide, in metres, a rift is at its floor at the edge, narrowing inland"))
		float RiftWidth = 60.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rift Depth", Category = "Rifts", DragSpeed = 1.0, Tooltip = "How deep, in metres, a rift is cut below the ground's height; deeper than the landmass is thick, open below"))
		float RiftDepth = 180.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rift Flare", Category = "Rifts", DragSpeed = 0.01, Tooltip = "How much wider a rift is a metre higher up, from its floor"))
		float RiftFlare = 0.15f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rift Roughness", Category = "Rifts", DragSpeed = 0.1, Tooltip = "How far, in metres, a rift's walls go in and out in ledges and hollows"))
		float RiftRoughness = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Water", Tooltip = "How many rivers, from high ground down to the sea, a lake, a rift or over the edge"))
		uint32_t Rivers = 3;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "River Width", Category = "Water", DragSpeed = 0.1, Tooltip = "How wide a river's bed is, in metres"))
		float RiverWidth = 12.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "River Depth", Category = "Water", DragSpeed = 0.1, Tooltip = "How deep a river's bed is cut, in metres"))
		float RiverDepth = 3.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Water", Tooltip = "How many lakes are dug in the plains"))
		uint32_t Lakes = 2;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Lake Radius", Category = "Water", DragSpeed = 1.0, Tooltip = "About how far a lake's basin reaches from its middle, in metres"))
		float LakeRadius = 90.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Lake Depth", Category = "Water", DragSpeed = 0.1, Tooltip = "How deep a lake's basin is dug, in metres"))
		float LakeDepth = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Tunnels", DragSpeed = 0.01, Tooltip = "How much tunnels there are, 0 to 1: most in rough land, none under the sea"))
		float Tunnels = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tunnel Width", Category = "Tunnels", DragSpeed = 0.1, Tooltip = "About how wide a tunnel is, in metres"))
		float TunnelWidth = 9.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tunnel Spacing", Category = "Tunnels", DragSpeed = 1.0, Tooltip = "About how far apart, in metres, tunnels wind"))
		float TunnelSpacing = 70.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Places steered to one kind of ground"))
		std::vector<SdfTerrainPlanPin> Pins;
	};

	struct TerrainWorldPlan;

	// Terrain as a signed field over a volume, so it can overhang, hollow out and float: a stack of
	// layers, each joined to what the ones before it made, evaluated in order, then the edits made to
	// it, in the order of their revisions. Its value at a point (SampleTerrain(), SdfTerrain.h) is a
	// function of these values alone, the same on every machine: negative inside, positive outside, 0
	// on the surface. Heights are along the entity's Y, around it. Meshed in chunks round the camera,
	// in the editor and the game (TerrainChunks.h), and drawn by the entity's Mesh Renderer.
	NYX_REFLECT(Component, meta = (DisplayName = "SDF Terrain"))
	struct SdfTerrainComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Terrain", Tooltip = "Another seed, another terrain of the same kind: every layer's noise changes"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Terrain", Tooltip = "Evaluated in order, each joined to what the ones before it made"))
		std::vector<SdfTerrainLayer> Layers{ SdfTerrainLayer{} };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "World Plan", Category = "Terrain", Tooltip = "What the world is made of and in what proportions"))
		SdfTerrainWorldPlan Plan;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "View Distance", Category = "Chunks", DragSpeed = 4.0, Tooltip = "How far from the camera, in metres, chunks are made and drawn, coarser the farther"))
		float ViewDistance = 1024.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Level Distance", Category = "Chunks", DragSpeed = 0.05, Tooltip = "How far each level of detail reaches, in its own chunks: the finest (32 m chunks of 1 m cells) this many chunks from the camera, each coarser one (cells twice the size) twice as far. 2 to 16"))
		float LevelDistance = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Finest Level", Category = "Chunks", Tooltip = "The finest level of detail, nearest the camera: 0 for cells of 1 m, -1 for cells of half a metre (16 m chunks)"))
		int32_t FinestLevel = 0;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Near Shadow Levels", Category = "Chunks", Tooltip = "How many of the finest levels of detail cast into every shadow cascade; each coarser one casts only from one cascade further out, and into the last at least"))
		int32_t NearShadowLevels = 2;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Chunks", Tooltip = "Dual: a vertex in each cell, sharp edges for the Sharp Materials; Marching Cubes: vertices on the grid's edges, all smooth"))
		ESdfTerrainMesher Mesher = ESdfTerrainMesher::Dual;

		// Each vertex's sky visibility in its mesh's Custom.x (FieldMeshSettings::SkyReach), for a
		// material that darkens what the sky lights in hollows; off, 1 everywhere, and chunks are made
		// about twice as fast. No material reads it yet.
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sky Shading", Category = "Chunks", Tooltip = "How much of the sky each vertex sees, for its material (the mesh's Custom.x); off, 1 everywhere and the chunks made about twice as fast. No material reads it yet"))
		bool bSkyShading = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sharp Materials", Category = "Chunks", Tooltip = "The material numbers whose surfaces keep sharp edges and corners, such as cliffs (Dual only)"))
		std::vector<uint32_t> SharpMaterials;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Material Colours", Category = "Chunks", UI = Color, Tooltip = "Each material number's vertex colour, linear RGB, from 0; a number past the list is white"))
		std::vector<glm::vec3> MaterialColors{ glm::vec3(0.13f, 0.30f, 0.05f), glm::vec3(0.22f, 0.14f, 0.07f), glm::vec3(0.26f, 0.25f, 0.23f) };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Collision Distance", Category = "Collision", DragSpeed = 1.0, Tooltip = "How far round every character and moving body, in metres, the finest chunks have collision while the world runs, and along where each is going for the next 2 s; 0: no collision"))
		float CollisionDistance = 64.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Debug View", Category = "Chunks", Tooltip = "Levels: a colour for each level of detail; Seams: those darker and the triangles joining levels bright"))
		ESdfTerrainDebugView DebugView = ESdfTerrainDebugView::None;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Bake After", Category = "Edits", Tooltip = "How many edits come near a brick of 8 m before it is baked: its values kept as they are then, so sampling there skips the layers and the edits before; undone with them. 0: never"))
		uint32_t BakeAfter = 16;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Activation Deadline", Category = "Edits", DragSpeed = 0.01, Tooltip = "How long, in seconds, an edit waits for its collision and its drawn chunks to be ready together before each goes live as soon as it is ready"))
		float ActivationDeadline = 0.25f;

		// What has been dug, filled and painted, and baked, in the order of the revisions
		// (AppendTerrainEdit())
		NYX_PROPERTY(Serialize)
		std::vector<SdfTerrainEdit> Edits;

		// The plan made from Seed and Plan, run-time only: made again when either changes
		// (GetTerrainWorldPlan(), TerrainWorldPlan.h)
		std::shared_ptr<const TerrainWorldPlan> Planned;
	};
}

#include "Generated/Runtime/SdfTerrainComponent.reflect.h"

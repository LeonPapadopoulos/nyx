#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
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
		Capsule,
		// The terrain's world plan (SdfTerrainComponent::Plan) made into ground: a floating landmass of
		// plains and rough land, its underside hanging below, sea floors, rifts from its edge, river
		// beds and tunnels where the plan put them; the plains' and rough land's noise from its own
		Plan
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Heightfield: ground at a height; Landmass: a floating mass with an underside; Tunnels and Chambers: caves; Detail: bumps on every surface before it; Box, Sphere and Capsule: shapes placed where they are; Plan: the World Plan made into ground"))
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

		// A Rift pin's way on: the rift comes in from the edge to the pin, then passes these in order
		// and ends past the last
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 4.0, Tooltip = "Rift: the places, along X and Z, the rift passes in order after the pin, ending past the last"))
		std::vector<glm::vec2> Path;
	};

	// What a hand override of a world plan does (SdfTerrainPlanOverride). Saved as its number.
	NYX_ENUM()
	enum class ESdfTerrainOverrideKind : uint8_t
	{
		// More of one kind of ground along its stroke, as a pin steers round itself
		Landform,
		// The plan's ground raised along its stroke by Height (lowered below 0), before its water
		Height,
		// A shape added to the ground or cut out of it at each of its points
		Stamp
	};

	// A stamp's shape. Saved as its number.
	NYX_ENUM()
	enum class ESdfTerrainStampShape : uint8_t
	{
		// A ball of Radius
		Sphere,
		// A box of Half Size each way, turned about its up by Yaw
		Box
	};

	// A hand override of a world plan (SdfTerrainWorldPlan::Overrides): drawn by hand over what the seed
	// makes, and made again with it every time the plan is (another seed, another value, a pin moved),
	// so it survives regeneration. Its points are from its pin's middle when it has one, so it moves
	// with the pin; else from the terrain's middle.
	NYX_REFLECT()
	struct SdfTerrainPlanOverride
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Off: as if it weren't there"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Landform: more of a kind of ground along it; Height: the ground raised or lowered along it; Stamp: a shape added or cut at each point"))
		ESdfTerrainOverrideKind Kind = ESdfTerrainOverrideKind::Landform;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The pin it belongs to, by its number from 0: its points are from the pin's middle and move with it. -1: the terrain's"))
		int32_t Pin = -1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 1.0, Tooltip = "Its stroke, or its stamps' places, along X and Z, from its pin's middle (or the terrain's)"))
		std::vector<glm::vec2> Points;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.5, Tooltip = "How far round its stroke it reaches, in metres, falling off smoothly; a sphere stamp's radius"))
		float Radius = 60.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Landform: the kind of ground it asks for"))
		ESdfTerrainLandform Landform = ESdfTerrainLandform::Plains;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.05, Tooltip = "Landform: how strongly, as a pin's Strength"))
		float Strength = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.5, Tooltip = "Height: how far, in metres, the ground is raised at its stroke (below 0: lowered); Stamp: its middle above the plan's ground"))
		float Height = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Stamp", Tooltip = "A ball or a box"))
		ESdfTerrainStampShape Shape = ESdfTerrainStampShape::Sphere;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Stamp", Tooltip = "Cut out of the ground; off: added to it"))
		bool bCarve = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Half Size", Category = "Stamp", DragSpeed = 0.5, Tooltip = "A box's half size each way, in metres"))
		glm::vec3 HalfSize{ 12.0f, 8.0f, 12.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Stamp", DragSpeed = 1.0, Tooltip = "A box's turn about its up, in degrees"))
		float Yaw = 0.0f;
	};

	// What a world is made of, in what proportions, worked out once from the terrain's seed
	// (MakeTerrainWorldPlan(), TerrainWorldPlan.h) on a grid of 16 m and made into ground by a Plan
	// layer: a floating landmass of plains and rough land with sea on it, rifts cut from its edge
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Tunnels", DragSpeed = 0.01, Tooltip = "How much of the land above the sea has tunnels under it, 0 to 1: most in rough land"))
		float Tunnels = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tunnel Width", Category = "Tunnels", DragSpeed = 0.1, Tooltip = "About how wide a tunnel is, in metres"))
		float TunnelWidth = 9.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tunnel Spacing", Category = "Tunnels", DragSpeed = 1.0, Tooltip = "About how far apart, in metres, tunnels wind"))
		float TunnelSpacing = 70.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Places steered to one kind of ground"))
		std::vector<SdfTerrainPlanPin> Pins;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Drawn by hand over what the seed makes, made again with it: kinds of ground, the ground raised or lowered, shapes added or cut; a pin's move with it"))
		std::vector<SdfTerrainPlanOverride> Overrides;
	};

	// A rule giving an SDF terrain's surfaces a material by where they lie and which way they face
	// (SdfTerrainComponent::MaterialRules): a surface of material Over that meets all its conditions
	// is drawn as Material instead
	NYX_REFLECT()
	struct SdfTerrainMaterialRule
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Off: as if it weren't there"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The material number of the surfaces it can change, as the layers and edits made them"))
		uint32_t Over = 0;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The material number it gives them"))
		uint32_t Material = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Min Slope", DragSpeed = 0.5, Tooltip = "In degrees from flat: 0 flat, 90 a wall, 180 an overhang's underside"))
		float MinSlope = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Max Slope", DragSpeed = 0.5, Tooltip = "In degrees from flat"))
		float MaxSlope = 180.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Min Height", DragSpeed = 0.5, Tooltip = "In metres, in the terrain's space"))
		float MinHeight = -100000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Max Height", DragSpeed = 0.5, Tooltip = "In metres, in the terrain's space"))
		float MaxHeight = 100000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Min Depth", DragSpeed = 0.5, Tooltip = "How far below the World Plan's ground height there, in metres: walls of rifts and tunnels deep down. 0 without a Plan layer"))
		float MinDepth = -100000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Max Depth", DragSpeed = 0.5, Tooltip = "How far below the World Plan's ground height there, in metres"))
		float MaxDepth = 100000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "On Landform", Tooltip = "Only where the World Plan made one kind of ground (with a Plan layer)"))
		bool bOnLandform = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "That kind of ground"))
		ESdfTerrainLandform Landform = ESdfTerrainLandform::Plains;
	};

	// A kind of place a world is made with, found on an SDF terrain with a Plan layer when its world is
	// made (SdfTerrainComponent::PlaceRules, PlaceOnTerrain(), TerrainPlacement.h): Count places on its
	// ground from above, each a footprint of Radius that is no steeper than Max Slope, has Clearance
	// free above it, lies on the kinds of ground allowed, within the Height Range, out of the water if
	// asked, apart from the others and near an earlier rule's place if asked. Where the seed's places
	// can't all be found, the rule's places are sought again with its next seed, up to 8 times, then
	// the Fallback places (the template's layout) stand in for the rest. The same on every machine.
	NYX_REFLECT()
	struct SdfTerrainPlaceRule
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Off: as if it weren't there"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "What its places are called, for other rules' Near and for Terrain Place Copies"))
		std::string Name = "Place";

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "How many places"))
		uint32_t Count = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.5, Tooltip = "How far round its middle, in metres, the ground must be fit"))
		float Radius = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Max Slope", DragSpeed = 0.5, Tooltip = "In degrees: how steeply the ground over its footprint may rise or fall from its middle"))
		float MaxSlope = 15.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.5, Tooltip = "How high, in metres, the space above its footprint must be free of terrain"))
		float Clearance = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Height Range", DragSpeed = 0.5, Tooltip = "Only where the ground is between these heights, in metres"))
		glm::vec2 HeightRange{ -1000.0f, 1000.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "On the plan's plains"))
		bool bPlains = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Rough Land", Category = "Ground", Tooltip = "On the plan's rough land"))
		bool bRoughland = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "Over the plan's tunnels"))
		bool bTunnels = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "In the plan's rifts, on their floors"))
		bool bRifts = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "Where the plan has sea"))
		bool bSea = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ground", Tooltip = "Not under the sea's level, nor under a lake's"))
		bool bDry = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distances", DragSpeed = 1.0, Tooltip = "The least distance, in metres, between two of its places"))
		float Spacing = 100.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distances", DragSpeed = 0.5, Tooltip = "The least distance, in metres, between its footprints and other rules' places' footprints"))
		float Apart = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distances", Tooltip = "Another, earlier rule's Name: each place within Near Distance of one of its places. Empty: anywhere"))
		std::string Near;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Near Distance", Category = "Distances", DragSpeed = 1.0, Tooltip = "Between how many metres from the nearest of Near's places"))
		glm::vec2 NearDistance{ 0.0f, 300.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Distances", DragSpeed = 4.0, Tooltip = "Where its places are, along X and Z, when not enough can be found by the rule: the template's layout"))
		std::vector<glm::vec2> Fallback;
	};

	// How a world made on an SDF terrain is crossed, for checking it when it is made (TerrainTraversal.h):
	// what a character is and can do, in metres
	NYX_REFLECT()
	struct SdfTerrainTraversal
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.1, Tooltip = "How far apart, in metres, the ground is looked at; at most 2048 a side, wider on larger worlds"))
		float Spacing = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.05, Tooltip = "How much room, in metres, a character needs over the ground"))
		float Headroom = 2.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Walk Slope", DragSpeed = 0.5, Tooltip = "How steep, in degrees, ground can be walked up"))
		float WalkSlope = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Step Height", DragSpeed = 0.05, Tooltip = "How high a step, in metres, walking takes besides the slope"))
		float StepHeight = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Climb Height", DragSpeed = 0.1, Tooltip = "How high, in metres, climbing goes up where walking can't"))
		float ClimbHeight = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Drop Height", DragSpeed = 0.1, Tooltip = "How far down, in metres, a character can drop safely, walking"))
		float DropHeight = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Gap Width", DragSpeed = 0.1, Tooltip = "How wide a gap, in metres, a jump crosses to ground about as high"))
		float GapWidth = 6.0f;
	};

	// Whether a made world's places can be reached (TerrainTraversal.h): every place of the To rule
	// from the first of the From rule, by walking and what else is allowed, each by at least
	// Approaches separate ways
	NYX_REFLECT()
	struct SdfTerrainRouteCheck
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Off: as if it weren't there"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The Place Rule whose first place the ways start from"))
		std::string From;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The Place Rule whose every place must be reached"))
		std::string To;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Up more than walking can, up to the Climb Height"))
		bool bClimb = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Over gaps up to the Gap Width"))
		bool bGap = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Through the sea and lakes"))
		bool bSwim = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "How many separate ways (sharing no 16 m cell) each place needs at least"))
		uint32_t Approaches = 1;
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "World Plan", Category = "Terrain", Tooltip = "What the world is made of and in what proportions, for a Plan layer to make into ground"))
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

		// Each chunk's vertices slide towards where the next coarser level has them as the camera goes
		// farther, all the way by the time that level takes the chunk's place (geomorphing), so a
		// chunk changing its level of detail doesn't jump; Dual only. The places ride in the mesh's
		// tangents (TerrainChunk::Morph).
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Level Blending", Category = "Chunks", Tooltip = "Each chunk's vertices slide towards the coarser level's shape as the camera goes farther, so a chunk changing its level of detail doesn't jump (Dual only)"))
		bool bLevelBlending = true;

		// Each vertex's sky visibility in its mesh's Custom.x (FieldMeshSettings::SkyReach), for a
		// material that darkens what the sky lights in hollows; off, 1 everywhere, and chunks are made
		// about twice as fast. No material reads it yet.
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sky Shading", Category = "Chunks", Tooltip = "How much of the sky each vertex sees, for its material (the mesh's Custom.x); off, 1 everywhere and the chunks made about twice as fast. No material reads it yet"))
		bool bSkyShading = false;

		// How much the terrain round the camera dims the sky's light on everything near it, by how much
		// of the sky it hides (TerrainOcclusion.h): tunnels, rifts, under overhangs; 0: not at all,
		// and no occlusion volume is kept
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sky Occlusion", Category = "Lighting", DragSpeed = 0.01, Tooltip = "0 to 1: how much the terrain round the camera dims the sky's light on everything near it, by how much of the sky it hides: tunnels, rifts, under overhangs, the underside; 0 not at all"))
		float SkyOcclusion = 1.0f;

		// Whether the terrain shades what the sun reaches past the sun's shadow maps (its Shadow
		// Distance), from the same volume: rifts, crests and the underside in shadow kilometres off
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Far Shadows", Category = "Lighting", Tooltip = "The terrain's shadows past the sun's Shadow Distance, from the same volume as the sky occlusion, out to about a kilometre from each place: rifts, crests and the underside in shadow far off"))
		bool bFarShadows = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sharp Materials", Category = "Chunks", Tooltip = "The material numbers whose surfaces keep sharp edges and corners, such as cliffs (Dual only)"))
		std::vector<uint32_t> SharpMaterials;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Material Colours", Category = "Chunks", UI = Color, Tooltip = "Each material number's vertex colour, linear RGB, from 0; a number past the list is white"))
		std::vector<glm::vec3> MaterialColors{ glm::vec3(0.13f, 0.30f, 0.05f), glm::vec3(0.22f, 0.14f, 0.07f), glm::vec3(0.26f, 0.25f, 0.23f) };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Material Rules", Category = "Chunks", Tooltip = "In order, the first a surface meets gives it its material: by its slope, height, depth below the World Plan's ground and the kind of ground there"))
		std::vector<SdfTerrainMaterialRule> MaterialRules;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Place Rules", Category = "Places", Tooltip = "Kinds of places found on the ground when the world is made, in order, each by its footprint, slope, clearance, kind of ground and distances (needs a Plan layer)"))
		std::vector<SdfTerrainPlaceRule> PlaceRules;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Places", Tooltip = "What a character is and can do, for the Route Checks"))
		SdfTerrainTraversal Traversal;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Route Checks", Category = "Places", Tooltip = "Whether the places can be reached: every place of one rule from the first of another, by what is allowed, by enough separate ways"))
		std::vector<SdfTerrainRouteCheck> RouteChecks;

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
		// (PrepareTerrainWorldPlan(), TerrainWorldPlan.h)
		std::shared_ptr<const TerrainWorldPlan> Planned;
	};
}

#include "Generated/Runtime/SdfTerrainComponent.reflect.h"

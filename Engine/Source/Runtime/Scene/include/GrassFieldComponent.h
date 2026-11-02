#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	// A field of single blades of grass over a surface, made on the GPU in tiles near the cameras and
	// drawn by the renderer, with nothing a blade on the CPU or in the scene file. Only these values
	// are saved. Whether a blade can grow at a place comes from them, the surface and the scatter
	// blockers alone, never from a camera or a quality setting; those only change how many of the
	// blades are drawn. Each entity with the component is one kind of grass.
	NYX_REFLECT(Component, meta = (DisplayName = "Grass Field"))
	struct GrassFieldComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Grass", DisplayName = "Grass", Tooltip = "Whether it is drawn"))
		bool bGrass = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Grass", Tooltip = "The entity to grow on: an SDF terrain, or its run-time mesh such as a ground's. None: this entity"))
		EntityGuid Surface;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Grass", Tooltip = "Another seed, other blades for the same values"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Grass", DragSpeed = 0.5, Tooltip = "Blades a square metre near the camera, 1 to 200"))
		float Density = 60.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Full Density Distance", Category = "Grass", DragSpeed = 0.5, Tooltip = "How far from the camera, in metres, the blades are at their full density; past it they thin out and grow wider"))
		float FullDensityDistance = 20.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Grass", DragSpeed = 1.0, Tooltip = "How far from the camera, in metres, the last blades are drawn, up to 500; near there they take the ground's colour"))
		float Range = 100.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Slope Range", Category = "Limits", DragSpeed = 0.1, Tooltip = "Only where the surface slopes between these, in degrees: 0 is flat, 90 a wall"))
		glm::vec2 SlopeRange{ 0.0f, 35.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Height Range", Category = "Limits", DragSpeed = 0.1, Tooltip = "Only where the surface is between these heights, in metres in the world"))
		glm::vec2 HeightRange{ -1000.0f, 1000.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Terrain Materials", Category = "Limits", Tooltip = "On an SDF terrain: only on its ground of these material numbers, below 32; none: on any"))
		std::vector<uint32_t> TerrainMaterials;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Use Blockers", Category = "Limits", Tooltip = "Keeps away from every Scatter Blocker of the world; off: grows through them"))
		bool bUseBlockers = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Blades", DragSpeed = 0.01, Tooltip = "How tall a blade is, in metres, on average"))
		float Height = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Height Variance", Category = "Blades", DragSpeed = 0.005, Tooltip = "How much blades differ in height, 0 to 1, as a share of Height"))
		float HeightVariance = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Blades", DragSpeed = 0.001, Tooltip = "How wide a blade is at its foot, in metres, on average"))
		float Width = 0.035f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Width Variance", Category = "Blades", DragSpeed = 0.005, Tooltip = "How much blades differ in width, 0 to 1, as a share of Width"))
		float WidthVariance = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Blades", DragSpeed = 0.5, Tooltip = "How far a blade's tip leans from upright, in degrees, 0 to 85, on average"))
		float Lean = 25.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Lean Variance", Category = "Blades", DragSpeed = 0.5, Tooltip = "How much blades differ in their lean, in degrees either way"))
		float LeanVariance = 15.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Blades", DragSpeed = 0.005, Tooltip = "How much a blade curves on its way up, 0 to 1: 0 straight, 1 an arc that droops at the tip"))
		float Bend = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Clump Size", Category = "Clumps", DragSpeed = 0.01, Tooltip = "How far apart the clumps blades grow in are, in metres"))
		float ClumpSize = 1.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Clump Height", Category = "Clumps", DragSpeed = 0.005, Tooltip = "How much of a blade's height comes from its clump, 0 to 1: 0 none, 1 all the clump's blades as tall"))
		float ClumpHeight = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Clump Facing", Category = "Clumps", DragSpeed = 0.005, Tooltip = "How much a clump's blades lean one way, 0 to 1: 0 each its own way, 1 all the clump's way"))
		float ClumpFacing = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Clump Pull", Category = "Clumps", DragSpeed = 0.005, Tooltip = "How much blades lean towards their clump's middle, 0 to 1, or away from it below 0"))
		float ClumpPull = 0.25f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Clump Color", Category = "Clumps", DragSpeed = 0.005, Tooltip = "How much clumps differ in colour, 0 to 1"))
		float ClumpColor = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Patch Size", Category = "Patches", DragSpeed = 0.5, Tooltip = "How far apart the patches of taller and drier grass are, in metres"))
		float PatchSize = 25.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Patch Height", Category = "Patches", DragSpeed = 0.005, Tooltip = "How much the patches change the blades' height, 0 to 1: at 1, from next to nothing to twice Height"))
		float PatchHeight = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Patch Dryness", Category = "Patches", DragSpeed = 0.005, Tooltip = "How much the patches turn the blades' tips towards Dry Color, 0 to 1"))
		float PatchDryness = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Root", Category = "Colours", UI = Color, Tooltip = "Linear RGB, at the foot of a blade"))
		glm::vec3 RootColor{ 0.08f, 0.20f, 0.04f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tip", Category = "Colours", UI = Color, Tooltip = "Linear RGB, at the tip of a blade"))
		glm::vec3 TipColor{ 0.30f, 0.50f, 0.11f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Dry Color", Category = "Colours", UI = Color, Tooltip = "Linear RGB, of the tips in dry patches"))
		glm::vec3 DryColor{ 0.55f, 0.50f, 0.18f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Color Variance", Category = "Colours", DragSpeed = 0.005, Tooltip = "How much single blades differ in brightness, 0 to 1"))
		float ColorVariance = 0.15f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Wind Response", Category = "Wind", DragSpeed = 0.01, Tooltip = "How far the wind bends the blades, 0 to 2: 0 not at all"))
		float WindResponse = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DragSpeed = 0.05, Tooltip = "The wind's speed, in metres a second, that bends a blade halfway to its most: stiffer grass needs more"))
		float Stiffness = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DragSpeed = 0.005, Tooltip = "How much single blades flutter on top of the bend, 0 to 1"))
		float Flutter = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Normal Rounding", Category = "Shading", DragSpeed = 0.005, Tooltip = "How rounded a blade's light is across its width, 0 to 1: 0 flat and hard-lit, like a facet, 1 soft and rounded"))
		float NormalRounding = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Self Shadow", Category = "Shading", DragSpeed = 0.005, Tooltip = "How dark the grass is low down and in the middle of its clumps, 0 to 1"))
		float SelfShadow = 0.45f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shading", DragSpeed = 0.005, Tooltip = "How bright blades glow with the sun behind them, 0 to 1"))
		float Translucency = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shading", DragSpeed = 0.005, Tooltip = "How rough the blades are, 0 to 1: lower, sharper highlights along them"))
		float Roughness = 0.55f;
	};
}

#include "Generated/Runtime/GrassFieldComponent.reflect.h"

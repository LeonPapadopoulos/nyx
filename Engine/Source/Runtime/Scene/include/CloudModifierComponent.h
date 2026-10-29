#pragma once

#include "ReflectionMacros.h"

#include <cstdint>
#include <vector>

namespace Nyx
{
	// What a cloud modifier's shape does to the volumetric clouds where it is. Saved as its number: new
	// shapes go at the end.
	NYX_ENUM()
	enum class ECloudModifierShape : uint8_t
	{
		// A vertical column where the clouds thin out: a hole through them
		ClearColumn,
		// A ring round the axis where the clouds grow denser and cover more, as if pushed aside and
		// piled up
		ThickenRing,
		// The clouds turned round the axis, further the longer it goes on, fastest at its radius: a
		// vortex
		Swirl,
	};

	// One shape of a Cloud Modifier, round its entity's position and the up axis
	NYX_REFLECT()
	struct CloudModifier
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Clear Column: a hole through the clouds; Thicken Ring: denser cloud round it; Swirl: the clouds turned round it"))
		ECloudModifierShape Shape = ECloudModifierShape::ClearColumn;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 1.0, Tooltip = "In metres: the column's radius, the ring's to its middle, where the swirl is fastest"))
		float Radius = 300.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 1.0, Tooltip = "In metres: how wide the column's soft edge is, how wide the ring is, and how soft the top and bottom are"))
		float Width = 200.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "Clear Column: how much thins out, 0 to 1 (1: none left in its middle). Thicken Ring: how much denser, 0 to 4. Swirl: how fast it turns at its radius, in turns a minute"))
		float Strength = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 10.0, Tooltip = "Where it starts, in metres above the world's height 0; with Top not above it, the whole band"))
		float Bottom = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 10.0, Tooltip = "Where it ends, in metres above the world's height 0; not above Bottom: the whole band"))
		float Top = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Start Time", DragSpeed = 0.1, Tooltip = "The world's time it starts at, in seconds"))
		float StartTime = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.1, Tooltip = "How long it lasts after its start, in seconds, before it fades out; 0 for ever"))
		float Duration = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Fade In", DragSpeed = 0.05, Tooltip = "Over how many seconds it grows to its strength from its start"))
		float FadeIn = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Fade Out", DragSpeed = 0.05, Tooltip = "Over how many seconds it fades away after its duration"))
		float FadeOut = 2.0f;
	};

	// Shapes that change the volumetric clouds' density where they are, round the entity's position
	// and the up axis, each animated by the world's time (Docs/Design/lightning.md, step 4): a column
	// cleared, a ring thickened, the clouds swirled. Without volumetric clouds they do nothing; a
	// view takes the nearest few (MaxCloudModifiers in CloudModifiers.h).
	NYX_REFLECT(Component, meta = (DisplayName = "Cloud Modifier"))
	struct CloudModifierComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Enabled", Tooltip = "Whether its shapes change the clouds"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Its shapes, each changing the clouds round the entity"))
		std::vector<Nyx::CloudModifier> Shapes;
	};
}

#include "Generated/Runtime/CloudModifierComponent.reflect.h"

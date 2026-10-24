#pragma once

#include "ReflectionMacros.h"

#include <cstdint>

namespace Nyx
{
	// What a scatter blocker keeps copies away from
	NYX_ENUM()
	enum class EScatterBlockerShape : uint8_t
	{
		// The entity's box, sphere or capsule collider, scaled as the physics scales it; the circle
		// when it has none
		Collider,
		// Round the entity, standing along each scatter's up: a post, a trunk
		Circle
	};

	// Keeps every scatter's copies away from its entity, such as grass out of a crate, a ledge or a
	// tree's trunk: none within its shape and the margin, and fewer further out over the falloff. Every
	// scatter of the world keeps to every blocker unless it says not to (ScatterComponent::bUseBlockers).
	// When a blocker is added, removed, edited or moved by more than a centimetre, only the cells it
	// reaches, before and after, are scattered again (UpdateScatters(), ScatterInstances.h).
	NYX_REFLECT(Component, meta = (DisplayName = "Scatter Blocker"))
	struct ScatterBlockerComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter Blocker", Tooltip = "Collider: the entity's box, sphere or capsule collider, or the circle when it has none. Circle: round the entity"))
		EScatterBlockerShape Shape = EScatterBlockerShape::Collider;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter Blocker", DragSpeed = 0.01, Tooltip = "The circle's radius in metres, scaled by the larger of the entity's X and Z scale, as a capsule's"))
		float Radius = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter Blocker", DragSpeed = 0.01, Tooltip = "No copy within this many metres of the shape"))
		float Margin = 0.1f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter Blocker", DragSpeed = 0.01, Tooltip = "Past the margin, the copies thin out over this many metres instead of stopping hard; 0: a hard edge"))
		float Falloff = 0.0f;
	};
}

#include "Generated/Runtime/ScatterBlockerComponent.reflect.h"

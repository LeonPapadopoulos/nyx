#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// Light from a point at the entity's position, such as a lamp or a fire, shining all round and
	// fading with the distance until it is gone at its range: as the square of the distance, softened
	// to reach 0 at the range (GetPointLightFalloff()). It lights every lit material, through the
	// world's fog as the sun's light is; it casts no shadows. The entity's rotation and scale don't
	// change it. A light whose range reaches no view isn't drawn.
	NYX_REFLECT(Component, meta = (DisplayName = "Point Light"))
	struct PointLightComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", UI = Color))
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.05, Tooltip = "How bright the light is: what a white matte surface facing it shows from 1 m away, as the sun's intensity is what it shows facing the sun"))
		float Intensity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.05, Tooltip = "How far the light reaches, in metres: it fades to nothing there. Shorter is cheaper"))
		float Range = 8.0f;
	};
}

#include "Generated/Runtime/PointLightComponent.reflect.h"

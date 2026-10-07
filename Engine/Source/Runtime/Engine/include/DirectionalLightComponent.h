#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx::Engine
{
	// Light from a far-away source like the sun, shining along the entity's forward axis (-Z).
	// The scene is lit by the first light marked as primary.
	NYX_REFLECT(Component, meta = (DisplayName = "Directional Light Component"))
	struct DirectionalLightComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.01))
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.01))
		float Intensity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.01, Tooltip = "Light that reaches every surface, including those facing away"))
		float Ambient = 0.18f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Primary", Tooltip = "Whether this light lights the scene"))
		bool bPrimary = true;
	};
}

#include "Generated/Runtime/DirectionalLightComponent.reflect.h"
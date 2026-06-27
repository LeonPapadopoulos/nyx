#pragma once

#include "ReflectionMacros.h"
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Nyx::Engine::Gameplay
{
	NYX_REFLECT(Component, meta = (DisplayName = "Transform"))
	struct TransformComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DragSpeed = 0.1))
		glm::vec3 Position{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", UI = Degrees))
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};
}

#include "Generated/Runtime/Input.reflect.h"
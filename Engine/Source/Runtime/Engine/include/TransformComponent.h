#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Nyx::Engine
{
	NYX_REFLECT(Component, meta = (DisplayName = "Transform Component"))
	struct TransformComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DragSpeed = 0.1))
		glm::vec3 Position{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", UI = Degrees))
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DragSpeed = 0.1))
		glm::vec3 Scale{ 1.0f };

		glm::mat4 ToMatrix() const
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
			const glm::mat4 rotation = glm::toMat4(Rotation);
			const glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);
			return translation * rotation * scale;
		}

		glm::vec3 GetRotationEulerRadians() const
		{
			return glm::eulerAngles(Rotation);
		}

		void SetRotationEulerRadians(const glm::vec3& eulerRadians)
		{
			Rotation = glm::normalize(glm::quat(eulerRadians));
		}
	};
}

#include "Generated/Runtime/TransformComponent.reflect.h"
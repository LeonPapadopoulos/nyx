#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Nyx::Engine
{
	struct TransformComponent
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
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
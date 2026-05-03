#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx::Engine
{
	struct TransformComponent
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 RotationRadians{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };

		glm::mat4 ToMatrix() const
		{
			glm::mat4 m = glm::translate(glm::mat4(1.0f), Position);
			m = glm::rotate(m, RotationRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, Scale);
			return m;
		}
	};
}
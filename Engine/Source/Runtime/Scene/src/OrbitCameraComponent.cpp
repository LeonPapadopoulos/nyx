#include "NyxPCH.h"
#include "OrbitCameraComponent.h"

#include <cmath>

namespace Nyx
{
	glm::vec3 GetGroundForward(float yaw)
	{
		// -Z turned about up
		return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
	}

	glm::vec3 GetGroundRight(float yaw)
	{
		return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
	}

	glm::vec3 GetGroundMove(float yaw, const glm::vec2& input)
	{
		const glm::vec3 move = GetGroundRight(yaw) * input.x + GetGroundForward(yaw) * input.y;
		const float length = glm::length(move);
		return length > 1.0f ? move / length : move;
	}
}

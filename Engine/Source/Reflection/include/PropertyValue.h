#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <variant>

namespace Nyx::Reflection
{
	using PropertyValue = std::variant<
		std::monostate,
		bool,
		int32_t,
		uint32_t,
		float,
		glm::vec2,
		glm::vec3,
		glm::vec4,
		glm::quat,
		std::string
	>;
}
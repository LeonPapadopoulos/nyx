#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <variant>

namespace Nyx::Editor
{
	using EditorValue = std::variant<
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

	inline bool NearlyEqual(float a, float b, float epsilon = 1e-5f)
	{
		return std::abs(a - b) <= epsilon;
	}

	inline bool NearlyEqual(const glm::vec2& a, const glm::vec2& b, float epsilon = 1e-5f)
	{
		return NearlyEqual(a.x, b.x, epsilon) &&
			NearlyEqual(a.y, b.y, epsilon);
	}

	inline bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-5f)
	{
		return NearlyEqual(a.x, b.x, epsilon) &&
			NearlyEqual(a.y, b.y, epsilon) &&
			NearlyEqual(a.z, b.z, epsilon);
	}

	inline bool NearlyEqual(const glm::vec4& a, const glm::vec4& b, float epsilon = 1e-5f)
	{
		return NearlyEqual(a.x, b.x, epsilon) &&
			NearlyEqual(a.y, b.y, epsilon) &&
			NearlyEqual(a.z, b.z, epsilon) &&
			NearlyEqual(a.w, b.w, epsilon);
	}

	inline bool NearlyEqual(const glm::quat& a, const glm::quat& b, float epsilon = 1e-5f)
	{
		return NearlyEqual(a.x, b.x, epsilon) &&
			NearlyEqual(a.y, b.y, epsilon) &&
			NearlyEqual(a.z, b.z, epsilon) &&
			NearlyEqual(a.w, b.w, epsilon);
	}

	inline bool AreEditorValuesEqual(const EditorValue& a, const EditorValue& b)
	{
		if (a.index() != b.index())
		{
			return false;
		}

		if (std::holds_alternative<std::monostate>(a))
		{
			return true;
		}

		if (std::holds_alternative<bool>(a))
		{
			return std::get<bool>(a) == std::get<bool>(b);
		}

		if (std::holds_alternative<int32_t>(a))
		{
			return std::get<int32_t>(a) == std::get<int32_t>(b);
		}

		if (std::holds_alternative<uint32_t>(a))
		{
			return std::get<uint32_t>(a) == std::get<uint32_t>(b);
		}

		if (std::holds_alternative<float>(a))
		{
			return NearlyEqual(std::get<float>(a), std::get<float>(b));
		}

		if (std::holds_alternative<glm::vec2>(a))
		{
			return NearlyEqual(std::get<glm::vec2>(a), std::get<glm::vec2>(b));
		}

		if (std::holds_alternative<glm::vec3>(a))
		{
			return NearlyEqual(std::get<glm::vec3>(a), std::get<glm::vec3>(b));
		}

		if (std::holds_alternative<glm::vec4>(a))
		{
			return NearlyEqual(std::get<glm::vec4>(a), std::get<glm::vec4>(b));
		}

		if (std::holds_alternative<glm::quat>(a))
		{
			return NearlyEqual(std::get<glm::quat>(a), std::get<glm::quat>(b));
		}

		if (std::holds_alternative<std::string>(a))
		{
			return std::get<std::string>(a) == std::get<std::string>(b);
		}

		return false;
	}
}
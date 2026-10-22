#include "NyxPCH.h"
#include "DebugDraw.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Nyx
{
	namespace
	{
		constexpr int CircleSegments = 24;
	}

	void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		if (Lines.size() >= MaxLines)
		{
			++DroppedLines;
			return;
		}
		Lines.push_back(DebugLine{ from, to, color, std::max(seconds, 0.0f) });
	}

	void DebugDraw::Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		Line(from, to, color, seconds);

		const glm::vec3 along = to - from;
		const float length = glm::length(along);
		if (length <= 1e-6f)
		{
			return;
		}

		// Two sides perpendicular to the arrow, from whichever world axis is least along it
		const glm::vec3 direction = along / length;
		const glm::vec3 helper = std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 side = glm::normalize(glm::cross(direction, helper));
		const glm::vec3 up = glm::cross(side, direction);

		const float headLength = std::max(length * 0.25f, 0.05f);
		const glm::vec3 headBase = to - direction * headLength;
		const float headWidth = headLength * 0.4f;
		Line(to, headBase + side * headWidth, color, seconds);
		Line(to, headBase - side * headWidth, color, seconds);
		Line(to, headBase + up * headWidth, color, seconds);
		Line(to, headBase - up * headWidth, color, seconds);
	}

	void DebugDraw::Box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color, float seconds)
	{
		Box(glm::mat4(1.0f), min, max, color, seconds);
	}

	void DebugDraw::Box(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax,
		const glm::vec3& color, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		// Corner i has max.x for bit 0, max.y for bit 1, max.z for bit 2
		std::array<glm::vec3, 8> corners;
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 local{ (i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y,
				(i & 4) ? localMax.z : localMin.z };
			corners[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
		}

		// The twelve edges: corners that differ in one bit
		for (int i = 0; i < 8; ++i)
		{
			for (int bit = 1; bit < 8; bit <<= 1)
			{
				if ((i & bit) == 0)
				{
					Line(corners[i], corners[i | bit], color, seconds);
				}
			}
		}
	}

	void DebugDraw::Sphere(const glm::vec3& center, float radius, const glm::vec3& color, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		const std::array<std::array<glm::vec3, 2>, 3> planes{ { { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0) },
			{ glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) }, { glm::vec3(0, 0, 1), glm::vec3(1, 0, 0) } } };
		for (const std::array<glm::vec3, 2>& plane : planes)
		{
			glm::vec3 previous = center + plane[0] * radius;
			for (int segment = 1; segment <= CircleSegments; ++segment)
			{
				const float angle = glm::two_pi<float>() * static_cast<float>(segment) / CircleSegments;
				const glm::vec3 point = center + (plane[0] * std::cos(angle) + plane[1] * std::sin(angle)) * radius;
				Line(previous, point, color, seconds);
				previous = point;
			}
		}
	}

	void DebugDraw::Axes(const glm::mat4& transform, float length, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		const glm::vec3 origin = glm::vec3(transform[3]);
		Line(origin, origin + glm::normalize(glm::vec3(transform[0])) * length, glm::vec3(1.0f, 0.0f, 0.0f), seconds);
		Line(origin, origin + glm::normalize(glm::vec3(transform[1])) * length, glm::vec3(0.0f, 1.0f, 0.0f), seconds);
		Line(origin, origin + glm::normalize(glm::vec3(transform[2])) * length, glm::vec3(0.0f, 0.4f, 1.0f), seconds);
	}

	void DebugDraw::Cross(const glm::vec3& point, float size, const glm::vec3& color, float seconds)
	{
		if constexpr (!Enabled)
		{
			return;
		}

		const float half = size * 0.5f;
		Line(point - glm::vec3(half, 0, 0), point + glm::vec3(half, 0, 0), color, seconds);
		Line(point - glm::vec3(0, half, 0), point + glm::vec3(0, half, 0), color, seconds);
		Line(point - glm::vec3(0, 0, half), point + glm::vec3(0, 0, half), color, seconds);
	}

	void DebugDraw::Advance(float seconds)
	{
		std::erase_if(Lines, [seconds](DebugLine& line)
			{
				if (line.SecondsLeft <= 0.0f)
				{
					return true;
				}
				line.SecondsLeft -= seconds;
				return line.SecondsLeft <= 0.0f;
			});
	}

	void DebugDraw::Clear()
	{
		Lines.clear();
	}
}

#pragma once

#include "CameraProjection.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	struct EditorCamera
	{
		// Where its view starts, in metres: also where the first shadow cascade does
		static constexpr float NearPlane = 0.1f;
		// Where it ends, in metres: far enough for kilometres of ground and things a kilometre tall;
		// depth is reversed floats (CameraProjection.h), as fine far out as near
		static constexpr float FarPlane = 20000.0f;
		// From the bottom of its picture to the top, in degrees
		static constexpr float VerticalFieldOfViewDegrees = 60.0f;

		float AspectRatio = 16.0f / 9.0f;

		glm::vec3 Position = glm::vec3(0.0f, 2.0f, 6.0f);
		glm::vec3 RotationRadians = glm::vec3(0.0f);

		glm::mat4 GetViewMatrix() const
		{
			glm::mat4 transform =
				glm::translate(glm::mat4(1.0f), Position) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));

			return glm::inverse(transform);
		}

		glm::mat4 GetProjectionMatrix() const
		{
			return MakePerspectiveProjection(glm::radians(VerticalFieldOfViewDegrees), AspectRatio, NearPlane, FarPlane);
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};
}
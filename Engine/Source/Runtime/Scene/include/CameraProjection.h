#pragma once

#include "CameraComponent.h"
#include "Entity.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <optional>

namespace Nyx
{
	// How a camera maps the world to the picture, the same for the renderer's scene views and for
	// going back from a pixel to the world, so the two can't drift apart

	// A half line in the world
	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		// Unit length
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
	};

	// The camera a world is seen through: the first camera marked primary, as the renderer finds
	// it, with its entity's transform (none: at the origin, unrotated)
	struct WorldCamera
	{
		Entity Owner;
		TransformComponent Transform;
		CameraComponent Camera;
	};

	std::optional<WorldCamera> FindPrimaryCamera(const Registry& world);

	// Width over height; 1 for a picture without height
	float GetAspectRatio(float width, float height);

	// From view space to Vulkan's clip space: right-handed, looking along -Z, Y turned so the top of
	// the picture is up, depth from -1 at the near plane to 1 at the far plane
	glm::mat4 MakePerspectiveProjection(float fovYRadians, float aspectRatio, float nearPlane, float farPlane);

	// From the world to the camera's view space: the inverse of the camera's transform
	glm::mat4 GetCameraView(const TransformComponent& transform);
	glm::mat4 GetCameraProjection(const CameraComponent& camera, float aspectRatio);

	// The ray through a point of a camera's picture, `viewportSize` pixels big, with `pixel` from
	// its top left corner, as ImGui gives the mouse over an image: from the near plane, away from
	// the camera. Pixel (0, 0) is the picture's top left corner, not the centre of its first pixel.
	Ray ScreenPointToRay(const TransformComponent& transform, const CameraComponent& camera,
		const glm::vec2& viewportSize, const glm::vec2& pixel);
}

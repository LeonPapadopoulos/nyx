#pragma once

#include "Extent2D.h"

#include <glm/glm.hpp>

namespace Nyx
{
	struct SceneViewCameraData
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };
		glm::mat4 InverseViewProjection{ 1.0f };
		glm::vec3 CameraWorldPos{ 0.0f };
		Extent2D Extent{};
	};
}
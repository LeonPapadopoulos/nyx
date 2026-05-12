#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx
{
	enum class EViewportCameraMode : uint8_t
	{
		EditorFreeCamera,
		ScenePrimaryCamera
	};

	struct ExtractedSceneGlobals
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };

		glm::vec3 CameraWorldPos{ 0.0f, 0.0f, 3.0f };
		glm::vec3 LightDirectionWS{ glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)) };
		glm::vec3 LightColor{ 1.0f, 0.98f, 0.95f };
		float Ambient = 0.18f;

		bool bHasCamera = false;
		bool bHasDirectionalLight = false;
	};
}
#pragma once

#include <glm/glm.hpp>

namespace Nyx::Engine
{
	struct DirectionalLightComponent
	{
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float Ambient = 0.18f;
		bool bPrimary = true;
	};
}
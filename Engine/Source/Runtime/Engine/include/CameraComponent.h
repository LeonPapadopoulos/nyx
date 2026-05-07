#pragma once

#include <glm/glm.hpp>

namespace Nyx::Engine
{
	struct CameraComponent
	{
		float FovYRadians = glm::radians(60.0f);
		float NearPlane = 0.1f;
		float FarPlane = 1000.0f;
		bool bPrimary = true;
	};
}
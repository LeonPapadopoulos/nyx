#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

namespace Nyx
{
	struct Material
	{
		vk::raii::Pipeline* Pipeline = nullptr;
		vk::raii::PipelineLayout* PipelineLayout = nullptr;

		float Reflectivity = 0.0f;
		bool bUseTexture = true;
		glm::vec3 Tint{ 1.0f, 1.0f, 1.0f };
	};
}
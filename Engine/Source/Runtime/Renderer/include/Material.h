#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

namespace Nyx
{
	struct Material
	{
		vk::raii::Pipeline* Pipeline = nullptr;
		vk::raii::PipelineLayout* PipelineLayout = nullptr;

		// Where its values are in the material buffer (VulkanBindless), written from the values below
		uint32_t MaterialOffset = 0;

		float Reflectivity = 0.0f;
		bool bUseTexture = true;
		glm::vec3 Tint{ 1.0f, 1.0f, 1.0f };
	};
}
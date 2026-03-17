#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Engine
{
	class VulkanUtil;

	class ImGuiVulkanUtil
	{
	public:
		ImGuiVulkanUtil(GLFWwindow* window, VulkanUtil* vulkan)
			: Window(window)
			, Vulkan(vulkan)
		{
		}

		~ImGuiVulkanUtil();

		// Core functionality methods for ImGui integration
		void Initialize(float width, float height);
		void Shutdown();
		void SetStyle(uint32_t index);

		// Frame-by-frame rendering operations
		void BeginFrame();
		void DrawFrame(vk::raii::CommandBuffer& commandBuffer);

		// Input event handling for interactive UI elements
		void HandleKey(int key, int scancode, int action, int mods);
		bool GetWantKeyCapture();
		void CharPressed(uint32_t key);

	private:
		void CreateDescriptorPool();

	private:
		GLFWwindow* Window = nullptr;
		VulkanUtil* Vulkan = nullptr;

		vk::raii::Device* Device = nullptr;
		vk::raii::DescriptorPool DescriptorPool{ nullptr };

		ImGuiStyle VulkanStyle;

		bool bInitialized = false;
	};

} // Engine

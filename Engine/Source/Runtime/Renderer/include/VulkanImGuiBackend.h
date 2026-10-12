#pragma once

#include "NyxEngineAPI.h"
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Nyx
{
	class VulkanRenderer;
}

namespace Nyx
{
	class NYXENGINE_API VulkanImGuiBackend
	{
	public:
		VulkanImGuiBackend(GLFWwindow* window, VulkanRenderer& vulkan);
		~VulkanImGuiBackend();

		// Core functionality methods for ImGui integration
		void Initialize(float width, float height);
		void Shutdown();
		void SetStyle(uint32_t index);

		// Frame-by-frame rendering operations
		void BeginFrame();
		// Records the main window's UI into the frame's command buffer
		void DrawFrame(vk::raii::CommandBuffer& commandBuffer);
		// Renders ImGui windows dragged out of the main window, each in its own OS window. ImGui
		// submits their command buffers right away, so call this after the frame's command buffer
		// is submitted: they show images the frame paints (scene views, painter canvases).
		void RenderPlatformWindows();

		// Input event handling for interactive UI elements
		void HandleKey(int key, int scancode, int action, int mods);
		bool GetWantKeyCapture();
		void CharPressed(uint32_t key);

	private:
		void CreateDescriptorPool();

	private:
		GLFWwindow* Window = nullptr;
		VulkanRenderer* Vulkan = nullptr;

		vk::raii::Device* Device = nullptr;
		vk::raii::DescriptorPool DescriptorPool{ nullptr };

		ImGuiStyle VulkanStyle;

		bool bInitialized = false;
	};
}

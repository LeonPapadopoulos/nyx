#pragma once

#include "NyxEngineAPI.h"
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

#include <map>
#include <utility>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Nyx
{
	class VulkanContext;
	class VulkanSwapchain;
}

namespace Nyx
{
	class NYXENGINE_API VulkanImGuiBackend
	{
	public:
		VulkanImGuiBackend(GLFWwindow* window, VulkanContext& context, VulkanSwapchain& swapchain);
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
		// Asks Windows to round the corners of the OS windows ImGui made for windows dragged out
		// of the main window, as the painted chrome's cards are rounded, or not while it is off
		void RoundOsWindowCorners();

	private:
		GLFWwindow* Window = nullptr;
		VulkanContext* Context = nullptr;
		VulkanSwapchain* Swapchain = nullptr;

		vk::raii::Device* Device = nullptr;
		vk::raii::DescriptorPool DescriptorPool{ nullptr };

		ImGuiStyle VulkanStyle;

		// Each OS window's handle, by its viewport, and whether it was asked to round its
		// corners: asked again only for a new OS window, or when the chrome is switched
		std::map<ImGuiID, std::pair<void*, bool>> RoundedOsWindows;

		bool bInitialized = false;
	};
}

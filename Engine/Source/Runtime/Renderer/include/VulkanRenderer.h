#pragma once
#include "Renderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include <imgui.h>

namespace Nyx
{
	class VulkanImGuiBackend;
}

namespace Nyx
{
	class VulkanRenderer : public IRenderer
	{
	public:
		VulkanRenderer();
		virtual ~VulkanRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window);
		virtual void Shutdown();

		virtual void BeginFrame();
		virtual void EndFrame();

		virtual void OnFramebufferResized();

		virtual void DrawFrame(const std::function<void()>& buildUI);
		
		virtual void WaitIdle();
	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		virtual ImTextureID GetSceneTextureId() const;
		virtual Extent2D GetSceneViewportExtent() const;
		virtual void EnsureSceneViewportSize(uint32_t width, uint32_t height);

		virtual void SetSceneViewportSize(uint32_t width, uint32_t height);
		virtual bool WasSceneViewportRecreatedThisFrame() const;

	private:
		void SetupVulkan(const char* applicationName, GLFWwindow* window);
		void SetupImGui();

		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

	private:
		void RecreateSwapChain();
		void WaitForValidFramebufferSize();

	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<VulkanImGuiBackend> ImGuiBackend;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		VulkanViewportTarget SceneViewport;
		uint32_t PendingSceneViewportWidth = 1280;
		uint32_t PendingSceneViewportHeight = 720;
		bool bSceneViewportResizePending = false;

		//vk::PhysicalDeviceFeatures DeviceFeatures;

		vk::raii::CommandPool CommandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> CommandBuffers;

		vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
		std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
		vk::raii::Fence InFlightFence{ nullptr };

		bool bRecreateSwapChain = false;
		bool bSceneViewportRecreatedThisFrame = false;
	};
}
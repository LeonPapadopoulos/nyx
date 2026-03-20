#pragma once
#include "NyxEngineAPI.h"
#include <functional>
#include <vulkan/vulkan_raii.hpp>

struct GLFWwindow;

namespace Nyx
{
	namespace Renderer
	{
		class NYXENGINE_API VulkanUtil
		{
		public:
			VulkanUtil();
			//virtual ~VulkanUtil() = default;

			void InitVulkan(const std::string& applicationName, GLFWwindow* window);
			void WaitIdle();

			void DrawFrame(const std::function<void(vk::raii::CommandBuffer&)>& recordUiPass);

			uint32_t GetMinImageCount() const { return MinImageCount; };
			vk::raii::Instance& GetInstance() { return Instance; }

			vk::raii::Device& GetDevice();
			vk::raii::PhysicalDevice& GetPhysicalDevice();
			uint32_t GetSwapChainImageCount() const { return static_cast<uint32_t>(SwapChainImages.size()); }
			//vk::Format GetSwapChainFormat() const;
			vk::raii::RenderPass& GetRenderPass() { return RenderPass; }
			const vk::Extent2D& GetSwapChainExtent();

			vk::raii::Queue& GetGraphicsQueue();
			uint32_t GetGraphicsQueueFamily() const { return GraphicsQueueFamily; };

			void OnFramebufferResized();

		private:
			void CreateInstance(const std::string& applicationName);
			void SetupDebugMessenger();
			void CreateSurface(GLFWwindow* window);
			void PickPhysicalDevice();
			void CreateLogicalDevice();
			void CreateSwapChain();
			void CreateImageViews();
			void CreateGraphicsPipeline();

			void CreateCommandPool();
			void CreateRenderPass();
			void CreateFramebuffers();
			void CreateCommandBuffers();
			void CreateSyncObjects();

		private:
			void CleanupSwapChain();
			void RecreateSwapChain();

		private:
			static uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities);
			static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats);
			static vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes);
			vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities);

			std::vector<const char*> GetRequiredInstanceExtensions();
			bool IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice);

		private:
			GLFWwindow* Window = nullptr;

			vk::raii::Context Context;
			vk::raii::Instance Instance = nullptr;
			vk::raii::DebugUtilsMessengerEXT DebugMessenger = nullptr;
			vk::raii::SurfaceKHR Surface = nullptr;
			vk::raii::PhysicalDevice PhysicalDevice = nullptr;
			vk::raii::Device Device = nullptr;
			vk::raii::SwapchainKHR SwapChain = nullptr;
			std::vector<vk::Image> SwapChainImages;
			vk::SurfaceFormatKHR SwapChainSurfaceFormat;
			vk::Extent2D SwapChainExtent;
			std::vector<vk::raii::ImageView> SwapChainImageViews;
			uint32_t MinImageCount = 0u;

			//vk::PhysicalDeviceFeatures DeviceFeatures;
			vk::raii::Queue GraphicsQueue = nullptr;
			uint32_t GraphicsQueueFamily = 0u;

			vk::raii::RenderPass RenderPass{ nullptr };
			std::vector<vk::raii::Framebuffer> Framebuffers;

			vk::raii::CommandPool CommandPool{ nullptr };
			std::vector<vk::raii::CommandBuffer> CommandBuffers;

			vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
			std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
			vk::raii::Fence InFlightFence{ nullptr };

			std::vector<const char*> RequiredDeviceExtension
			{
				vk::KHRSwapchainExtensionName
			};

			bool bRecreateSwapChain = false;
		};
	}

} // Engine
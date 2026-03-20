#pragma once
#include <vulkan/vulkan_raii.hpp>

struct GLFWwindow;

namespace Nyx
{
    class VulkanContext
    {
    public:
        void Initialize(const char* applicationName, GLFWwindow* window);
        void Shutdown();

        vk::raii::Instance& GetInstance();
        vk::raii::PhysicalDevice& GetPhysicalDevice();
        vk::raii::Device& GetDevice();
        vk::raii::SurfaceKHR& GetSurface();
        vk::raii::Queue& GetGraphicsQueue();

        uint32_t GetGraphicsQueueFamily() const;

    private:
        void CreateInstance(const char* applicationName);
        void SetupDebugMessenger();
        void CreateSurface(GLFWwindow* window);
        void PickPhysicalDevice();
        void CreateLogicalDevice();

    private:
        std::vector<const char*> GetRequiredInstanceExtensions();
        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice);

    private:
        vk::raii::Context Context;
        vk::raii::Instance Instance{ nullptr };
        vk::raii::DebugUtilsMessengerEXT DebugMessenger{ nullptr };
        vk::raii::SurfaceKHR Surface{ nullptr };

        vk::raii::PhysicalDevice PhysicalDevice{ nullptr };
        vk::raii::Device Device{ nullptr };
        vk::raii::Queue GraphicsQueue{ nullptr };

        uint32_t GraphicsQueueFamily = 0;

        std::vector<const char*> RequiredDeviceExtension
        {
            vk::KHRSwapchainExtensionName
        };
    };
}
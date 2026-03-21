#pragma once
#include "VulkanContext.h"
#include <vulkan/vulkan_raii.hpp>

struct GLFWwindow;

namespace Nyx
{
    class VulkanSwapchain
    {
    public:
        void Initialize(VulkanContext& context, GLFWwindow* window);
        void Shutdown();

        void Recreate(VulkanContext& context, GLFWwindow* window);
        void Cleanup();

        vk::raii::SwapchainKHR& GetSwapchain();
        const std::vector<vk::Image>& GetImages() const;
        const std::vector<vk::raii::ImageView>& GetImageViews() const;
        const std::vector<vk::raii::Framebuffer>& GetFramebuffers() const;

        vk::raii::RenderPass& GetRenderPass();

        const vk::Extent2D& GetExtent() const;
        const vk::SurfaceFormatKHR& GetSurfaceFormat() const;

        uint32_t GetMinImageCount() const;
        uint32_t GetImageCount() const;

    private:
        void CreateSwapchain(VulkanContext& context, GLFWwindow* window);
        void CreateImageViews(VulkanContext& context);
        void CreateRenderPass(VulkanContext& context);
        void CreateFramebuffers(VulkanContext& context);

        static uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
        static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
        static vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& presentModes);
        static vk::Extent2D ChooseSwapExtent(GLFWwindow* window, const vk::SurfaceCapabilitiesKHR& capabilities);

    private:
        vk::raii::SwapchainKHR Swapchain{ nullptr };
        std::vector<vk::Image> SwapchainImages;
        std::vector<vk::raii::ImageView> SwapchainImageViews;

        vk::SurfaceFormatKHR SurfaceFormat{};
        vk::Extent2D Extent{};
        uint32_t MinImageCount = 0;

        vk::raii::RenderPass RenderPass{ nullptr };
        std::vector<vk::raii::Framebuffer> Framebuffers;
    };
}
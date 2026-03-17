#include "Enginepch.h"
#include "WindowsWindow.h"
#include "Log.h"
#include "Assertions.h"
#include "VulkanUtil.h"
#include "ImGuiVulkanUtil.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace Engine
{
    static bool bGLFWInitialized = false;

    IWindow* IWindow::Create(const WindowSpecs& specs)
    {
        return new WindowsWindow(specs);
    }

    WindowsWindow::WindowsWindow(const WindowSpecs& specs)
    {
        Initialize(specs);
    }

    WindowsWindow::~WindowsWindow()
    {
        Shutdown();
    }

    void WindowsWindow::OnUpdate()
    {
        glfwPollEvents();

        VulkanHandler->DrawFrame(
            [this](vk::raii::CommandBuffer& commandBuffer)
            {
                ImGui->BeginFrame();

                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
                {
                    ImGui::DockSpaceOverViewport();
                }

                // @todo LP: Draw Game Engine Editor
                {
                    ImGui::ShowDemoWindow();
                }

                ImGui->DrawFrame(commandBuffer);
            });
    }

    unsigned int WindowsWindow::GetWidth()
    {
        return Data.Width;
    }

    unsigned int WindowsWindow::GetHeight()
    {
        return Data.Height;
    }

    void WindowsWindow::SetVSync(bool enabled)
    {
        // @todo: 
        if (enabled)
        {
            glfwSwapInterval(1);
        }
        else
        {
            glfwSwapInterval(0);
        }
        Data.bVSync = enabled;
    }

    bool WindowsWindow::IsVSync() const
    {
        return Data.bVSync;
    }

    bool WindowsWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(Window);
    }

    void WindowsWindow::Initialize(const WindowSpecs& specs)
    {
        Data.Title = specs.Title;
        Data.Width = specs.Width;
        Data.Height = specs.Height;

        LOG_INFO("Creating Window. {0} {1} {2}", Data.Title, Data.Width, Data.Height);

        if (!bGLFWInitialized)
        {
            // @todo: glfwTerminate on system shutdown
            int success = glfwInit();
            ASSERT(success && "Failed to initialize GLFW!");

            bGLFWInitialized = true;
        }

        // Custom Titlebar
        glfwWindowHint(GLFW_TITLEBAR, true);

        // Tell glfw to not set up the default OpenGL context
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // @todo: Update renderer to handle resizing
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        Window = glfwCreateWindow(
            static_cast<int>(Data.Width),
            static_cast<int>(Data.Height),
            Data.Title.c_str(),
            nullptr,
            nullptr);

        glfwMakeContextCurrent(Window);

        // @todo: what's the most sensible UserPointer to set?
        glfwSetWindowUserPointer(Window, this);
        glfwSetTitlebarHitTestCallback(Window, [](GLFWwindow* window, int posX, int posY, int* hit)
            {
                WindowsWindow* windowsWindow = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
                *hit = windowsWindow->IsTitleBarHovered();
            });

        SetVSync(true);

        // @todo: SetupVulkan
        ASSERT(glfwVulkanSupported() && "Currently only Vulkan is supported.");

        SetupVulkan();

        ASSERT(VulkanHandler->GetMinImageCount() >= 2 && "Failed to fulfill ImGui Vulkan requirements.");
        ASSERT(VulkanHandler->GetSwapChainImageCount() >= VulkanHandler->GetMinImageCount() && "Failed to fulfill ImGui Vulkan requirements.");

        ImGui = new ImGuiVulkanUtil(Window, VulkanHandler);
        ImGui->Initialize(
            static_cast<float>(VulkanHandler->GetSwapChainExtent().width),
            static_cast<float>(VulkanHandler->GetSwapChainExtent().height));
    }

    void WindowsWindow::Shutdown()
    {
        if (VulkanHandler)
        {
            VulkanHandler->WaitIdle();
        }

        delete ImGui;
        ImGui = nullptr;

        delete VulkanHandler;
        VulkanHandler = nullptr;

        glfwDestroyWindow(Window);
        glfwTerminate();
    }

    void WindowsWindow::SetupVulkan()
    {
        ASSERT(!VulkanHandler && "Create VulkanHandler only once!");
        VulkanHandler = new Engine::VulkanUtil();
        VulkanHandler->InitVulkan(Data.Title, Window);
    }
}

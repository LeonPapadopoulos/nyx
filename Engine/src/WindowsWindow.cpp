#include "Enginepch.h"
#include "WindowsWindow.h"
#include "Log.h"
#include "Assertions.h"
#include <GLFW/glfw3.h>

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
        glfwSwapBuffers(Window);
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

        Window = glfwCreateWindow(
            static_cast<int>(Data.Width),
            static_cast<int>(Data.Height),
            Data.Title.c_str(),
            nullptr,
            nullptr);

        glfwMakeContextCurrent(Window);
        glfwSetWindowUserPointer(Window, &Data);
        SetVSync(true);
    }

    void WindowsWindow::Shutdown()
    {
        glfwDestroyWindow(Window);
    }
}

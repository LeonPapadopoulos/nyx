#pragma once

#include "FrameRateLimiter.h"
#include "Window.h"

#include <functional>
#include <memory>

struct GLFWwindow;

namespace Nyx
{
	class WindowsStartupBanner;
}

namespace Nyx::Input
{
	class GlfwInputBackend;
}

namespace Nyx
{
	class WindowsWindow : public IWindow
	{
	public:
		WindowsWindow(const WindowSpecs& specs);
		virtual ~WindowsWindow();

		void WaitForNextFrame() override;
		void PollEvents() override;
		bool WaitIfMinimized() override;
		void DrawUI(const std::function<void()>& drawUI) override;
		GLFWwindow* GetNativeWindow() override { return Window; }
		void SetFramebufferResizedCallback(std::function<void()> callback) override { OnFramebufferResized = std::move(callback); }
		void StartUIInput() override;
		Input::IInputBackend* GetInputBackend() override;

		unsigned int GetWidth() override;
		unsigned int GetHeight() override;

		WindowPlacement GetPlacement() const override;
		void SetPlacement(const WindowPlacement& placement) override;

		void SetFrameRateCap(uint32_t framesPerSecond) override;
		uint32_t GetFrameRateCap() const override;
		virtual bool ShouldClose() const override;
		void RequestClose() override;

		void SetFrameContent(WindowFrameContent content) override;
		void SetStartupStatus(const std::string& status) override;
		void FinishStartup() override;

		bool IsTitleBarHovered() const { return bTitlebarHovered; }

	private:
		void Initialize(const WindowSpecs& specs);
		void Shutdown();

		static void ApplyRoundedCorners(GLFWwindow* window);

		// The strips of the frame, filled, and what goes into them; heights of 0 for strips that
		// aren't there. Also works out whether the mouse is where it drags the window.
		void DrawFrameStrips(float titlebarHeight, float toolbarHeight, float statusBarHeight);
		// The window controls and what goes left of them; returns the controls' width
		float DrawTitlebar(const WindowFrameArea& area);

		bool IsMaximized() const;

		// Keeps Placement up to date with where the window is, except while it is minimized
		void TrackPlacement();

	private:
		GLFWwindow* Window = nullptr;
		std::function<void()> OnFramebufferResized;

		struct WindowData
		{
			std::string Title;
			unsigned int Width;
			unsigned int Height;
			bool bCustomTitlebar;
			bool bResizable;
			bool bVisible;
		};

		WindowData Data;
		FrameRateLimiter FrameLimiter;
		bool bTitlebarHovered = false;

		WindowFrameContent FrameContent;

		// Where the window is, or was before it was minimized (GetPlacement()); while it is hidden
		// before FinishStartup(), whether it will appear maximized
		WindowPlacement Placement;

		// Shown while the application starts up; released by FinishStartup().
		std::unique_ptr<WindowsStartupBanner> StartupBanner;

		// Keyboard and mouse. Destroyed after the GLFW window, whose callbacks it is.
		std::unique_ptr<Input::GlfwInputBackend> InputBackend;
	};
}

#pragma once

#include "Window.h"

#include <functional>
#include <memory>

struct GLFWwindow;

namespace Nyx
{
	class IRenderer;
	class WindowsStartupBanner;
}

namespace Nyx
{
	class WindowsWindow : public IWindow
	{
	public:
		WindowsWindow(const WindowSpecs& specs);
		virtual ~WindowsWindow();

		void PollEvents() override;
		void DrawFrame(const std::function<void()>& drawUI) override;
		IRenderer& GetRenderer() override { return *Renderer; }

		unsigned int GetWidth() override;
		unsigned int GetHeight() override;

		virtual void SetVSync(bool enabled) override;
		virtual bool IsVSync() const override;
		virtual bool ShouldClose() const override;

		void SetTitlebarMenu(std::function<void(float buttonHeight)> drawMenu) override;
		void SetStartupStatus(const std::string& status) override;
		void FinishStartup() override;

		bool IsTitleBarHovered() const { return bTitlebarHovered; }

	private:
		void Initialize(const WindowSpecs& specs);
		void Shutdown();

		static void ApplyRoundedCorners(GLFWwindow* window);

		// @todo: Is there a better place to put this?
		void DrawUserInterface();
		void DrawTitlebar(float titlebarHeight);
		void DrawTitlebarMenuBar(float titlebarHeight);
		void DrawMenubar();

		bool IsMaximized() const;

		static void GLFW_ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

		void DrawDockspaceHost(const std::function<void()>& drawUI);

	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<IRenderer> Renderer = nullptr;

		struct WindowData
		{
			std::string Title;
			unsigned int Width;
			unsigned int Height;
			bool bCustomTitlebar;
			bool bResizable;
			bool bVSync;
		};

		WindowData Data;
		bool bTitlebarHovered = false;

		std::function<void(float buttonHeight)> TitlebarMenuCallback;

		// Shown while the application starts up; released by FinishStartup().
		std::unique_ptr<WindowsStartupBanner> StartupBanner;
	};
}

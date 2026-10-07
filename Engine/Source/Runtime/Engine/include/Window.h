#pragma once
#include "NyxEngineAPI.h"
#include <functional>
#include <string>

namespace Nyx
{
	class IRenderer;

	struct WindowSpecs
	{
		std::string Title = "Nyx Engine";
		unsigned int Width = 1280;
		unsigned int Height = 720;
		bool bUseCustomTitlebar = true;
		bool bResizable = true;

		// Shows the startup banner (see Assets/Startup) while the application is starting.
		bool bShowStartupBanner = true;
	};

	// Platform Abstraction
	class NYXENGINE_API IWindow
	{
	public:
		virtual ~IWindow()
		{
		}

		virtual void PollEvents() = 0;

		// Renders one frame. drawUI is called inside the window's UI frame, so the
		// windows it draws can be docked into the window's dock area.
		virtual void DrawFrame(const std::function<void()>& drawUI) = 0;

		virtual IRenderer& GetRenderer() = 0;

		virtual unsigned int GetWidth() = 0;
		virtual unsigned int GetHeight() = 0;

		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual bool ShouldClose() const = 0;

		// Sets the function that draws the menu at the left of the titlebar (e.g. File, Window).
		// There is one menu; pass nullptr to remove it. buttonHeight fits the titlebar.
		virtual void SetTitlebarMenu(std::function<void(float buttonHeight)> drawMenu) = 0;

		// With WindowSpecs::bShowStartupBanner, a banner is shown from window creation until FinishStartup().
		virtual void SetStartupStatus(const std::string& status) = 0;
		virtual void FinishStartup() = 0;

		static IWindow* Create(const WindowSpecs& specs = WindowSpecs());
	};
}

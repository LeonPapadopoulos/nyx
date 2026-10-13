#pragma once
#include "NyxEngineAPI.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Nyx
{
	class IRenderer;

	// A point on the screen, in screen coordinates
	struct WindowPosition
	{
		int X = 0;
		int Y = 0;
	};

	struct WindowSpecs
	{
		std::string Title = "Nyx Engine";

		// The client area, without titlebar and borders
		unsigned int Width = 1280;
		unsigned int Height = 720;

		// Where the window's outer top-left corner goes. Unset: the system chooses.
		std::optional<WindowPosition> Position;

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

		// Returns once the next frame may start under the frame rate cap; at once without one.
		// Called before the frame's events are polled, so the frame works with the newest input.
		virtual void WaitForNextFrame() = 0;

		virtual void PollEvents() = 0;

		// Renders one frame. drawUI is called inside the window's UI frame, so the
		// windows it draws can be docked into the window's dock area. While the window is
		// minimized, nothing is drawn: it waits briefly for events instead, so the frame loop
		// (and the layers' OnUpdate) keeps running at a low rate.
		virtual void DrawFrame(const std::function<void()>& drawUI) = 0;

		virtual IRenderer& GetRenderer() = 0;

		virtual unsigned int GetWidth() = 0;
		virtual unsigned int GetHeight() = 0;

		// With vsync (the default), each frame is shown at the monitor's next refresh, and the
		// frame loop waits for it: the frame rate is at most the monitor's. Without, frames are
		// drawn as fast as they can be, and each refresh shows the newest one.
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		// At most this many frames per second, 0 for no cap. Under vsync too, e.g. 30 on a 60 Hz
		// monitor.
		virtual void SetFrameRateCap(uint32_t framesPerSecond) = 0;
		virtual uint32_t GetFrameRateCap() const = 0;

		virtual bool ShouldClose() const = 0;

		// Makes ShouldClose() true, as if the user closed the window
		virtual void RequestClose() = 0;

		// Sets the function that draws the menu at the left of the titlebar (e.g. File, Window).
		// There is one menu; pass nullptr to remove it. buttonHeight fits the titlebar.
		virtual void SetTitlebarMenu(std::function<void(float buttonHeight)> drawMenu) = 0;

		// With WindowSpecs::bShowStartupBanner, a banner is shown from window creation until FinishStartup().
		virtual void SetStartupStatus(const std::string& status) = 0;
		virtual void FinishStartup() = 0;

		static IWindow* Create(const WindowSpecs& specs = WindowSpecs());
	};
}

#pragma once
#include "NyxEngineAPI.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Nyx::Input
{
	class IInputBackend;
}

namespace Nyx
{
	class IRenderer;
	class IEditorRenderer;

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

	// A strip of the window's frame, in ImGui's screen coordinates
	struct WindowFrameArea
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
	};

	// What the application draws into the window's frame. Each function is called inside the
	// window's UI frame with ImGui's cursor at its area's top left, and is drawn on the strip's
	// own fill. The strips' heights and colours come from the UI theme (Nyx::UI::Theme).
	struct WindowFrameContent
	{
		// The titlebar, left of the window controls; with the custom titlebar only. Without it,
		// the titlebar shows the window's title. Where nothing is hovered, the titlebar drags the
		// window and a double click maximises it, as Windows' own.
		std::function<void(const WindowFrameArea& area)> DrawTitlebar;
		// A strip under the titlebar across the window, e.g. menus and a toolbar; the empty parts
		// drag the window too. None when unset.
		std::function<void(const WindowFrameArea& area)> DrawToolbar;
		// A strip across the window's bottom, under the dock space. None when unset.
		std::function<void(const WindowFrameArea& area)> DrawStatusBar;
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

		// The renderer the window draws with: its scene part, and all of it for the editor and the UI
		virtual IRenderer& GetRenderer() = 0;
		virtual IEditorRenderer& GetEditorRenderer() = 0;

		// Where the window's keyboard and mouse input comes from; it lives as long as the window
		virtual Input::IInputBackend* GetInputBackend() = 0;

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

		// Sets what the application draws into the titlebar, a toolbar and a status bar. Pass an
		// empty WindowFrameContent to remove it, e.g. before what it calls into goes away.
		virtual void SetFrameContent(WindowFrameContent content) = 0;

		// With WindowSpecs::bShowStartupBanner, a banner is shown from window creation until FinishStartup().
		virtual void SetStartupStatus(const std::string& status) = 0;
		virtual void FinishStartup() = 0;

		static IWindow* Create(const WindowSpecs& specs = WindowSpecs());
	};
}

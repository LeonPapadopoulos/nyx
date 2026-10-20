#include "ImGuiSource.h"
#include "NyxPCH.h"
#include "WindowsWindow.h"
#include "GlfwInputBackend.h"
#include "WindowsStartupBanner.h"
#include "Log.h"
#include "Assertions.h"
#include "ImGuiTheme.h"
#include "UIChrome.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"
#include "Paths.h"

#include <windows.h>
#include <dwmapi.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "backends/imgui_impl_glfw.h"
#include <imgui_internal.h>
#include <imgui.h>

namespace Nyx
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

	Input::IInputBackend* WindowsWindow::GetInputBackend()
	{
		return InputBackend.get();
	}

	void WindowsWindow::WaitForNextFrame()
	{
		FrameLimiter.WaitForNextFrame();
	}

	void WindowsWindow::PollEvents()
	{
		glfwPollEvents();
	}

	bool WindowsWindow::WaitIfMinimized()
	{
		// A minimized window has no framebuffer to draw into, and drawing would block in the
		// renderer until the window is restored. Waiting briefly instead keeps the frame loop
		// running, e.g. so the editor link keeps working.
		int framebufferWidth = 0;
		int framebufferHeight = 0;
		glfwGetFramebufferSize(Window, &framebufferWidth, &framebufferHeight);
		if (framebufferWidth == 0 || framebufferHeight == 0)
		{
			glfwWaitEventsTimeout(0.05);
			return true;
		}
		return false;
	}

	unsigned int WindowsWindow::GetWidth()
	{
		return Data.Width;
	}

	unsigned int WindowsWindow::GetHeight()
	{
		return Data.Height;
	}

	void WindowsWindow::SetFrameRateCap(uint32_t framesPerSecond)
	{
		FrameLimiter.SetFramesPerSecond(framesPerSecond);
	}

	uint32_t WindowsWindow::GetFrameRateCap() const
	{
		return FrameLimiter.GetFramesPerSecond();
	}

	bool WindowsWindow::ShouldClose() const
	{
		return glfwWindowShouldClose(Window);
	}

	void WindowsWindow::RequestClose()
	{
		glfwSetWindowShouldClose(Window, GLFW_TRUE);
	}

	void WindowsWindow::SetFrameContent(WindowFrameContent content)
	{
		FrameContent = std::move(content);
	}

	void WindowsWindow::SetStartupStatus(const std::string& status)
	{
		if (StartupBanner)
		{
			// Status texts are plain ASCII, so widening each character is enough.
			StartupBanner->SetStatus(std::wstring(status.begin(), status.end()));
		}
	}

	void WindowsWindow::FinishStartup()
	{
		StartupBanner.reset();
		glfwShowWindow(Window);
	}

	void WindowsWindow::Initialize(const WindowSpecs& specs)
	{
		if (specs.bShowStartupBanner)
		{
			StartupBanner = std::make_unique<WindowsStartupBanner>(Paths::GetEngineAssetsDir() / "Startup");
		}
		SetStartupStatus("Creating the window");
		Data.Title = specs.Title;
		Data.Width = specs.Width;
		Data.Height = specs.Height;
		Data.bCustomTitlebar = specs.bUseCustomTitlebar;
		Data.bResizable = specs.bResizable;

		LOG_INFO("Creating Window. {0} {1} {2}", Data.Title, Data.Width, Data.Height);

		if (!bGLFWInitialized)
		{
			// @todo: glfwTerminate on system shutdown
			int success = glfwInit();
			ASSERT(success && "Failed to initialize GLFW!");

			bGLFWInitialized = true;
		}
		// Tell glfw to NOT set up the default OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_TITLEBAR, Data.bCustomTitlebar ? GLFW_FALSE : GLFW_TRUE);

		// @todo: Update renderer to handle resizing
		glfwWindowHint(GLFW_RESIZABLE, Data.bResizable ? GLFW_TRUE : GLFW_FALSE);

		//GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
		//const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);

		//int monitorX, monitorY;
		//glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);

		// @todo: Figure out why transparent client space area doesn't seem to be supported,
		// and find a way to make it work anyways; Purpose being a window-wide startup animation
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

		// By constructing it in an invisible state first, we can get rid of the windows titlebar
		// (assuming we use a custom one) without the need of having to manually resize the window first
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		{
			Window = glfwCreateWindow(
				static_cast<int>(Data.Width),
				static_cast<int>(Data.Height),
				Data.Title.c_str(),
				nullptr,
				nullptr);

			// HACK LP: this caues the window to ultimately appear titlebar-less from the start
			int w, h;
			glfwGetWindowSize(Window, &w, &h);
			glfwSetWindowSize(Window, w + 1, h);
			glfwSetWindowSize(Window, w, h);
		}

		// Placed while still hidden, so it doesn't jump. GLFW positions the client area; the frame
		// around it (titlebar and borders) goes left of and above that.
		if (specs.Position)
		{
			int frameLeft = 0;
			int frameTop = 0;
			glfwGetWindowFrameSize(Window, &frameLeft, &frameTop, nullptr, nullptr);
			glfwSetWindowPos(Window, specs.Position->X + frameLeft, specs.Position->Y + frameTop);
		}

		const bool bTransparent = glfwGetWindowAttrib(Window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
		ASSERT(bTransparent && "Transparent framebuffer not supported on this platform/path.");

		ApplyRoundedCorners(Window);

		// @todo: what's the most sensible UserPointer to set?
		glfwSetWindowUserPointer(Window, this);
		glfwSetTitlebarHitTestCallback(Window, [](GLFWwindow* window, int posX, int posY, int* hit)
			{
				WindowsWindow* windowsWindow = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
				*hit = windowsWindow->IsTitleBarHovered();
#if NYX_UI_INSPECTION
				if (UI::IsInspectingSources()) *hit = false;
#endif
			});
		glfwSetFramebufferSizeCallback(Window, [](GLFWwindow* window, int width, int height)
			{
				auto* self = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
				if (self->OnFramebufferResized)
				{
					self->OnFramebufferResized();
				}
			});

		// Before the renderer sets up ImGui's GLFW backend, which then calls the input backend's
		// callbacks after its own
		InputBackend = std::make_unique<Input::GlfwInputBackend>(Window);

		ASSERT(glfwVulkanSupported() && "Currently only Vulkan is supported.");

		// The window stays hidden behind the startup banner until FinishStartup().
	}

	void WindowsWindow::Shutdown()
	{
		glfwDestroyWindow(Window);
		Window = nullptr;
		glfwTerminate();
	}

	void WindowsWindow::ApplyRoundedCorners(GLFWwindow* window)
	{
		ASSERT(window != nullptr);
		HWND hwnd = glfwGetWin32Window(window);
		if (!hwnd)
		{
			return;
		}

		const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
		const HRESULT hr = DwmSetWindowAttribute(
			hwnd,
			DWMWA_WINDOW_CORNER_PREFERENCE,
			&pref,
			sizeof(pref));

		// optional: log hr if failed
	}

	void WindowsWindow::DrawFrameStrips(float titlebarHeight, float toolbarHeight, float statusBarHeight)
	{
		const ImVec2 cursorBackup = ImGui::GetCursorPos();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const float left = viewport->Pos.x;
		const float top = viewport->Pos.y;
		const float width = viewport->Size.x;
		const float bottom = viewport->Pos.y + viewport->Size.y;

		const WindowFrameArea titlebar{ left, top, width, titlebarHeight };
		const WindowFrameArea toolbar{ left, top + titlebarHeight, width, toolbarHeight };
		const WindowFrameArea statusBar{ left, bottom - statusBarHeight, width, statusBarHeight };

		// The whole window, its padding included, which the window's own clip rect leaves out
		ImGui::PushClipRect(viewport->Pos, ImVec2(left + width, bottom), false);

		// Each strip's fill, and a hairline where it meets the dock space's side
		const bool bPaintedChrome = Nyx::UI::IsPaintedChromeEnabled();
		const auto fill = [&](float minY, float maxY, const glm::vec4& color)
		{
			if (maxY <= minY)
			{
				return;
			}
			if (bPaintedChrome)
			{
				Nyx::UI::GetWindowPainter().AddRect(UI::PainterRect{ { left, minY }, { left + width, maxY } }, UI::PainterStyle::Filled(color));
			}
			else
			{
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, minY), ImVec2(left + width, maxY), ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a)));
			}
		};
		fill(titlebar.Y, titlebar.Y + titlebarHeight, theme.Titlebar);
		fill(toolbar.Y, toolbar.Y + toolbarHeight, theme.Toolbar);
		fill(statusBar.Y, bottom, theme.StatusBar);
		const float topEdge = top + titlebarHeight + toolbarHeight;
		if (topEdge > top)
		{
			fill(topEdge - 1.0f, topEdge, theme.BarBorder);
		}
		if (titlebarHeight > 0.0f && toolbarHeight > 0.0f)
		{
			fill(toolbar.Y - 1.0f, toolbar.Y, theme.BarBorder);
		}
		if (statusBarHeight > 0.0f)
		{
			fill(statusBar.Y, statusBar.Y + 1.0f, theme.BarBorder);
		}

		const float windowControlsWidth = titlebarHeight > 0.0f ? DrawTitlebar(titlebar) : 0.0f;
		if (toolbarHeight > 0.0f)
		{
			ImGui::SetCursorScreenPos(ImVec2(toolbar.X, toolbar.Y));
			FrameContent.DrawToolbar(toolbar);
		}
		if (statusBarHeight > 0.0f)
		{
			ImGui::SetCursorScreenPos(ImVec2(statusBar.X, statusBar.Y));
			FrameContent.DrawStatusBar(statusBar);
		}

		ImGui::PopClipRect();

		// The window is dragged by the titlebar, left of its window controls, and by the toolbar:
		// where neither a widget nor a popup or menu over them is hovered
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool bOverTitlebar = mouse.x >= left && mouse.x < left + width - windowControlsWidth && mouse.y >= top && mouse.y < top + titlebarHeight;
		const bool bOverToolbar = mouse.x >= left && mouse.x < left + width && mouse.y >= toolbar.Y && mouse.y < toolbar.Y + toolbarHeight;
		bTitlebarHovered = (bOverTitlebar || bOverToolbar) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

		ImGui::SetCursorPos(cursorBackup);
	}

	float WindowsWindow::DrawTitlebar(const WindowFrameArea& area)
	{
		const bool bIsMaximized = IsMaximized();

		// The window controls at the right end, as tall as the titlebar
		const glm::vec2 controlSize(46.0f, area.Height);
		const float controlsWidth = controlSize.x * 3.0f;
		const float controlsLeft = area.X + area.Width - controlsWidth;

		ImGui::SetCursorScreenPos(ImVec2(controlsLeft, area.Y));
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarMinimize", Nyx::UI::EWindowControl::Minimize, controlSize)))
		{
			glfwIconifyWindow(Window);
		}

		ImGui::SetCursorScreenPos(ImVec2(controlsLeft + controlSize.x, area.Y));
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarMaxRestore", bIsMaximized ? Nyx::UI::EWindowControl::Restore : Nyx::UI::EWindowControl::Maximize, controlSize)))
		{
			if (bIsMaximized)
				glfwRestoreWindow(Window);
			else
				glfwMaximizeWindow(Window);
		}

		ImGui::SetCursorScreenPos(ImVec2(controlsLeft + controlSize.x * 2.0f, area.Y));
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarClose", Nyx::UI::EWindowControl::Close, controlSize)))
		{
			glfwSetWindowShouldClose(Window, GLFW_TRUE);
		}

		// The application's content left of them, cut off there in a narrow window, or else the
		// window's title in the middle
		if (FrameContent.DrawTitlebar)
		{
			ImGui::PushClipRect(ImVec2(area.X, area.Y), ImVec2(controlsLeft, area.Y + area.Height), true);
			ImGui::SetCursorScreenPos(ImVec2(area.X, area.Y));
			FrameContent.DrawTitlebar(WindowFrameArea{ area.X, area.Y, area.Width - controlsWidth, area.Height });
			ImGui::PopClipRect();
		}
		else
		{
			const ImVec2 textSize = ImGui::CalcTextSize(Data.Title.c_str());
			const ImVec2 textPos(area.X + (area.Width - textSize.x) * 0.5f, area.Y + (area.Height - textSize.y) * 0.5f);
			ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 255), Data.Title.c_str());
		}

		return controlsWidth;
	}

	bool WindowsWindow::IsMaximized() const
	{
		return static_cast<bool>(glfwGetWindowAttrib(Window, GLFW_MAXIMIZED));
	}

	void WindowsWindow::StartUIInput()
	{
		InputBackend->HookImGuiWindows();
	}

	void WindowsWindow::DrawUI(const std::function<void()>& drawUI)
	{
		// @todo: Expose as parameter
		const bool bColoredBorderOnFocus = true;

		const bool bAppFocused = glfwGetWindowAttrib(Window, GLFW_FOCUSED) == GLFW_TRUE;
		const bool bIsMaximized = IsMaximized();
		ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
		windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		// The dock space fills it exactly; it never scrolls
		windowFlags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		// With the painted chrome, the backdrop is painted instead of the window's background,
		// and the cards keep their gap from the window's edges too.
		const bool bPaintedChrome = Nyx::UI::IsPaintedChromeEnabled();
		if (bPaintedChrome)
		{
			windowFlags |= ImGuiWindowFlags_NoBackground;
		}
		const float padding = bPaintedChrome ? Nyx::UI::GetTheme().CardGap : 2.0f;

		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, bIsMaximized ? 0.0f : 5.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
		const glm::vec4& backdrop = Nyx::UI::GetTheme().Backdrop;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(backdrop.r, backdrop.g, backdrop.b, backdrop.a));

		Nyx::UI::Begin("DockSpaceWindow", nullptr, windowFlags);
		Nyx::UI::PaintBackdrop();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);

		// The frame's strips: the titlebar (custom only), and the toolbar and status bar when the
		// application draws into them
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const float titlebarHeight = Data.bCustomTitlebar ? theme.TitlebarHeight : 0.0f;
		const float toolbarHeight = FrameContent.DrawToolbar ? theme.ToolbarHeight : 0.0f;
		const float statusBarHeight = FrameContent.DrawStatusBar ? theme.StatusBarHeight : 0.0f;
		// The strips lie in the window's padding and beyond the dock space, so what they hold
		// doesn't count as the window's content: the status bar's would make it scroll.
		ImGuiWindow* hostWindow = ImGui::GetCurrentWindow();
		const ImVec2 contentMax = hostWindow->DC.CursorMaxPos;
		DrawFrameStrips(titlebarHeight, toolbarHeight, statusBarHeight);
		hostWindow->DC.CursorMaxPos = contentMax;

		// The dock space lies the window's padding (with the chrome, the cards' gap) from the strips
		// above and below it: the cards' margin, where their shadows and glows may reach, then ends
		// at the strips.
		ImGui::SetCursorPosY(titlebarHeight + toolbarHeight + padding);

		ImGuiStyle& style = ImGui::GetStyle();
		const float prevMinWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;
		ImGui::DockSpace(ImGui::GetID("MyDockspace"), ImVec2(0.0f, -statusBarHeight));
		style.WindowMinSize.x = prevMinWinSizeX;

		// Draw the application's UI (for the editor: its panels), which can dock into the dock space above
		drawUI();

		// Draw the colored border last, so it's on top of everything else. It belongs to the custom
		// window frame; with the system titlebar, Windows draws the frame.
		if (Data.bCustomTitlebar && bColoredBorderOnFocus && bAppFocused)
		{
			const ImVec2 winMin = ImGui::GetWindowPos();
			const ImVec2 winMax(winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight());

			if (bPaintedChrome)
			{
				const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
				const UI::PainterRect border{ { winMin.x, winMin.y }, { winMax.x, winMax.y } };
				Nyx::UI::GetForegroundPainter(viewport).AddRoundedRect(border, bIsMaximized ? 0.0f : theme.WindowRounding,
					UI::PainterStyle::Outlined(theme.WindowFocusBorder, theme.WindowFocusBorderWidth));
			}
			else
			{
				ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

				// rounding to match that of win11 windows
				const float rounding = bIsMaximized ? 0.0f : 10.0f;
				// thickness to match the look of a focussed visual studio window
				const float borderThickness = 3.0f;

				const glm::vec4& focusBorder = Nyx::UI::GetTheme().WindowFocusBorder;
				const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(focusBorder.r, focusBorder.g, focusBorder.b, focusBorder.a));

				drawList->AddRect(
					winMin,
					winMax,
					borderColor,
					rounding,
					0,
					borderThickness);
			}
		}

		ImGui::End(); // DockSpaceWindow
	}
}

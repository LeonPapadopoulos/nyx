#include "ImGuiSource.h"
#include "NyxPCH.h"
#include "WindowsWindow.h"
#include "WindowsStartupBanner.h"
#include "Log.h"
#include "Assertions.h"
#include "Renderer.h"
#include "ImGuiTheme.h"
#include "SceneViewTypes.h"
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
#include "backends/imgui_impl_vulkan.h"
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

	void WindowsWindow::WaitForNextFrame()
	{
		FrameLimiter.WaitForNextFrame();
	}

	void WindowsWindow::PollEvents()
	{
		glfwPollEvents();
	}

	void WindowsWindow::DrawFrame(const std::function<void()>& drawUI)
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
			return;
		}

		Renderer->DrawFrame(
			[this, &drawUI]()
			{
				DrawDockspaceHost(drawUI);
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
		// The renderer presents its frames; before it exists, it takes this when created.
		Data.bVSync = enabled;
		if (Renderer)
		{
			Renderer->SetVSync(enabled);
		}
	}

	bool WindowsWindow::IsVSync() const
	{
		return Data.bVSync;
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

	void WindowsWindow::SetTitlebarMenu(std::function<void(float buttonHeight)> drawMenu)
	{
		TitlebarMenuCallback = std::move(drawMenu);
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
			StartupBanner = std::make_unique<WindowsStartupBanner>(Paths::GetAssetsDir() / "Startup");
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
				self->Renderer->OnFramebufferResized();
			});

		glfwSetScrollCallback(Window, &WindowsWindow::GLFW_ScrollCallback);

		SetVSync(true);

		ASSERT(glfwVulkanSupported() && "Currently only Vulkan is supported.");
		Renderer = Nyx::CreateRenderer();
		Renderer->SetVSync(Data.bVSync);
		SetStartupStatus("Starting Vulkan and preparing rendering resources");
		Renderer->Initialize(Data.Title.c_str(), Window);

		// The window stays hidden behind the startup banner until FinishStartup().
	}

	void WindowsWindow::Shutdown()
	{
		if (Renderer)
		{
			Renderer->WaitIdle();
			Renderer->Shutdown();
			Renderer.reset();
		}

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

	void WindowsWindow::DrawTitlebar(float titlebarHeight)
	{
		const ImVec2 cursorBackup = ImGui::GetCursorPos();

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImDrawList* windowDrawList = ImGui::GetWindowDrawList();

		const bool bIsMaximized = IsMaximized();

		const ImVec2 titlebarMin = ImVec2(viewport->Pos.x, viewport->Pos.y);
		const ImVec2 titlebarMax = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + titlebarHeight);

		// Background: with the painted chrome, the titlebar lies on the backdrop.
		if (!Nyx::UI::IsPaintedChromeEnabled())
		{
			windowDrawList->AddRectFilled(titlebarMin, titlebarMax, Colors::Theme::titlebar);
		}

		// Debug entire titlebar area
		//drawList->AddRect(titlebarMin, titlebarMax, IM_COL32(255, 255, 0, 255));

		// Title
		const ImVec2 textSize = ImGui::CalcTextSize(Data.Title.c_str());
		const ImVec2 textPos(
			titlebarMin.x + (viewport->Size.x - textSize.x) * 0.5f,
			titlebarMin.y + (titlebarHeight - textSize.y) * 0.5f);
		windowDrawList->AddText(textPos, IM_COL32(255, 255, 255, 255), Data.Title.c_str());

		// Button layout
		const ImVec2 buttonSize(46.0f, titlebarHeight);
		const float rightEdge = titlebarMax.x;

		const ImVec2 closePos(rightEdge - buttonSize.x, titlebarMin.y);
		const ImVec2 maxPos(rightEdge - buttonSize.x * 2.0f, titlebarMin.y);
		const ImVec2 minPos(rightEdge - buttonSize.x * 3.0f, titlebarMin.y);
		const glm::vec2 controlSize(buttonSize.x, buttonSize.y);

		ImGui::SetCursorScreenPos(minPos);
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarMinimize", Nyx::UI::EWindowControl::Minimize, controlSize)))
		{
			glfwIconifyWindow(Window);
		}

		ImGui::SetCursorScreenPos(maxPos);
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarMaxRestore", bIsMaximized ? Nyx::UI::EWindowControl::Restore : Nyx::UI::EWindowControl::Maximize, controlSize)))
		{
			if (bIsMaximized)
				glfwRestoreWindow(Window);
			else
				glfwMaximizeWindow(Window);
		}

		ImGui::SetCursorScreenPos(closePos);
		if (NYX_UI(Nyx::UI::WindowControlButton("##TitlebarClose", Nyx::UI::EWindowControl::Close, controlSize)))
		{
			glfwSetWindowShouldClose(Window, GLFW_TRUE);
		}

		// Drag region = full titlebar except button area
		const float leftReservedWidth = 170.0f;
		const ImVec2 dragMin = ImVec2(titlebarMin.x + leftReservedWidth, titlebarMin.y);
		const ImVec2 dragMax = ImVec2(minPos.x, titlebarMax.y);

		// Debug titlebar drag-area-only
		// drawList->AddRect(dragMin, dragMax, IM_COL32(0, 255, 0, 255));

		bTitlebarHovered = ImGui::IsMouseHoveringRect(dragMin, dragMax, false);

		if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())
		{
			bTitlebarHovered = false;
		}

		ImGui::SetCursorPos(cursorBackup);
	}

	void WindowsWindow::DrawTitlebarMenuBar(float titlebarHeight)
	{
		const ImVec2 cursorBackup = ImGui::GetCursorPos();

		const float menuStartX = 8.0f;
		const float menuStartY = 2.0f;
		const float buttonHeight = titlebarHeight - 4.0f;

		ImGui::SetCursorPos(ImVec2(menuStartX, menuStartY));

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));

		// The menu's contents come from the application (for the editor: File, Window).
		if (TitlebarMenuCallback)
		{
			TitlebarMenuCallback(buttonHeight);
		}

		ImGui::PopStyleVar(2);

		// Restore layout cursor so this function does not consume extra vertical layout space.
		ImGui::SetCursorPos(cursorBackup);
	}

	bool WindowsWindow::IsMaximized() const
	{
		return static_cast<bool>(glfwGetWindowAttrib(Window, GLFW_MAXIMIZED));
	}

	void WindowsWindow::GLFW_ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
	{
		WindowsWindow* windowsWindow = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
		if (!windowsWindow)
		{
			return;
		}

		windowsWindow->Renderer->OnMouseWheelScrolled(yOffset);
	}

	void WindowsWindow::DrawDockspaceHost(const std::function<void()>& drawUI)
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
		ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Theme::background);

		Nyx::UI::Begin("DockSpaceWindow", nullptr, windowFlags);
		Nyx::UI::PaintBackdrop();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);

		const float titlebarHeight = Data.bCustomTitlebar ? 32.0f : 0.0f;
		if (Data.bCustomTitlebar)
		{
			DrawTitlebar(titlebarHeight);
			DrawTitlebarMenuBar(titlebarHeight);

			if (bPaintedChrome)
			{
				// Exactly the cards' gap below the titlebar: the cards' margin, where their shadows
				// and glows may reach, then ends at the titlebar.
				ImGui::SetCursorPosY(titlebarHeight + padding);
			}
			else
			{
				// @todo LP: Figure out where the padding between titlebar-drag-zone and dockarea comes from
				const float paddingToRemoveHACK = 8.0f;

				// Reserve space so docked content starts below the custom titlebar
				NYX_UI(ImGui::Dummy(ImVec2(0.0f, titlebarHeight - paddingToRemoveHACK)));
			}
		}

		ImGuiStyle& style = ImGui::GetStyle();
		const float prevMinWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;
		ImGui::DockSpace(ImGui::GetID("MyDockspace"));
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
				const Engine::PainterRect border{ { winMin.x, winMin.y }, { winMax.x, winMax.y } };
				Nyx::UI::GetForegroundPainter(viewport).AddRoundedRect(border, bIsMaximized ? 0.0f : theme.WindowRounding,
					Engine::PainterStyle::Outlined(theme.WindowFocusBorder, theme.WindowFocusBorderWidth));
			}
			else
			{
				ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

				// rounding to match that of win11 windows
				const float rounding = bIsMaximized ? 0.0f : 10.0f;
				// thickness to match the look of a focussed visual studio window
				const float borderThickness = 3.0f;

				constexpr ImU32 borderColor = Colors::Theme::windowBorderER_1;

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

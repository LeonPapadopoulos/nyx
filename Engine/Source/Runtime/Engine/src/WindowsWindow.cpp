#include "NyxPCH.h"
#include "WindowsWindow.h"
#include "Log.h"
#include "Assertions.h"
#include "Renderer.h"
#include "ImGuiTheme.h"

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

namespace Icon::Util
{
    enum class ETitlebarButton
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    static float s_CenteredSquareRectPadding = 8.0f;
    static float s_IconThickness = 1.5f;

    static ImRect MakeCenteredSquareRect(const ImVec2& min, const ImVec2& max, float padding = 0.0f)
    {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float side = ImMax(1.0f, ImMin(width, height) - padding * 2.0f);

        const ImVec2 center(
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f
        );

        const ImVec2 half(side * 0.5f, side * 0.5f);

        return ImRect(
            ImVec2(center.x - half.x, center.y - half.y),
            ImVec2(center.x + half.x, center.y + half.y)
        );
    }

    static void DrawMinimizeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        const ImRect r = MakeCenteredSquareRect(min, max, s_CenteredSquareRectPadding);

        const float y = std::floor((r.Min.y + r.Max.y) * 0.5f) + 0.5f;
        const float padX = (r.Max.x - r.Min.x) * 0.15f;

        drawList->AddLine(
            ImVec2(r.Min.x + padX, y),
            ImVec2(r.Max.x - padX, y),
            color,
            s_IconThickness
        );
    }

    static void DrawMaximizeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        // HACK: An equally sized square will always 'look' bigger than the 'close' icon,
        // so we make it inherently smaller
        const float iconSize = ImMax(0.0f, s_CenteredSquareRectPadding + 2.0f);
        const ImRect r = MakeCenteredSquareRect(min, max, iconSize);

        drawList->AddRect(
            ImVec2(std::floor(r.Min.x) + 0.5f, std::floor(r.Min.y) + 0.5f),
            ImVec2(std::floor(r.Max.x) - 0.5f, std::floor(r.Max.y) - 0.5f),
            color,
            0.0f,
            0,
            s_IconThickness
        );
    }

    static void DrawRestoreIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        const ImRect r = MakeCenteredSquareRect(min, max, s_CenteredSquareRectPadding);

        const float side = r.GetWidth();
        const float offset = side * 0.18f;

        const ImVec2 backMin(
            std::floor(r.Min.x + offset) + 0.5f,
            std::floor(r.Min.y) + 0.5f
        );
        const ImVec2 backMax(
            std::floor(r.Max.x) - 0.5f,
            std::floor(r.Max.y - offset) - 0.5f
        );

        const ImVec2 frontMin(
            std::floor(r.Min.x) + 0.5f,
            std::floor(r.Min.y + offset) + 0.5f
        );
        const ImVec2 frontMax(
            std::floor(r.Max.x - offset) - 0.5f,
            std::floor(r.Max.y) - 0.5f
        );

        drawList->AddRect(backMin, backMax, color, 0.0f, 0, s_IconThickness);
        drawList->AddRect(frontMin, frontMax, color, 0.0f, 0, s_IconThickness);
    }

    static void DrawCloseIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        const ImRect r = MakeCenteredSquareRect(min, max, s_CenteredSquareRectPadding);

        const float pad = r.GetWidth() * 0.18f;

        drawList->AddLine(
            ImVec2(r.Min.x + pad, r.Min.y + pad),
            ImVec2(r.Max.x - pad, r.Max.y - pad),
            color,
            s_IconThickness
        );

        drawList->AddLine(
            ImVec2(r.Max.x - pad, r.Min.y + pad),
            ImVec2(r.Min.x + pad, r.Max.y - pad),
            color,
            s_IconThickness
        );
    }

    static void DrawTitlebarButtonIcon(ETitlebarButton type, ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        switch (type)
        {
        case ETitlebarButton::Minimize:
            DrawMinimizeIcon(drawList, min, max, color);
            break;
        case ETitlebarButton::Maximize:
            DrawMaximizeIcon(drawList, min, max, color);
            break;
        case ETitlebarButton::Restore:
            DrawRestoreIcon(drawList, min, max, color);
            break;
        case ETitlebarButton::Close:
            DrawCloseIcon(drawList, min, max, color);
            break;
        default:
            break;
        }
    }

    static bool DrawTitlebarButton(
        ImDrawList* drawList,
        const char* id,
        ETitlebarButton type,
        const ImVec2& pos,
        const ImVec2& size,
        ImU32 normalBg,
        ImU32 hoveredBg,
        ImU32 pressedBg,
        ImU32 iconColor)
    {
        const ImVec2 oldCursorPos = ImGui::GetCursorPos();

        ImGui::SetCursorScreenPos(pos);
        const bool pressed = ImGui::InvisibleButton(id, size);

        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        ImU32 bgColor = normalBg;
        if (held)
            bgColor = pressedBg;
        else if (hovered)
            bgColor = hoveredBg;

        drawList->AddRectFilled(min, max, bgColor);
        DrawTitlebarButtonIcon(type, drawList, min, max, iconColor);

        ImGui::SetCursorPos(oldCursorPos);
        return pressed;
    }
}

namespace Nyx
{
    static bool bGLFWInitialized = false;

    IWindow* IWindow::Create(const WindowSpecs & specs)
    {
        return new WindowsWindow(specs);
    }

    WindowsWindow::WindowsWindow(const WindowSpecs & specs)
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

        Renderer->DrawFrame(
            [this]()
            {
                // @todo LP: Draw Game Engine Editor
                DrawUserInterface();
                ImGui::ShowDemoWindow();
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

    void WindowsWindow::Initialize(const WindowSpecs & specs)
    {
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
        // Custom Titlebar
        glfwWindowHint(GLFW_TITLEBAR, Data.bCustomTitlebar ? GLFW_FALSE : GLFW_TRUE);

        // @todo: Update renderer to handle resizing
        glfwWindowHint(GLFW_RESIZABLE, Data.bResizable ? GLFW_TRUE : GLFW_FALSE);

        //GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
        //const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);

        //int monitorX, monitorY;
        //glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);

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
        glfwShowWindow(Window);

        const bool bTransparent = glfwGetWindowAttrib(Window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
        ASSERT(bTransparent && "Transparent framebuffer not supported on this platform/path.");

        ApplyRoundedCorners(Window);

        // @todo: what's the most sensible UserPointer to set?
        glfwSetWindowUserPointer(Window, this);
        glfwSetTitlebarHitTestCallback(Window, [](GLFWwindow* window, int posX, int posY, int* hit)
            {
                WindowsWindow* windowsWindow = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
                *hit = windowsWindow->IsTitleBarHovered();
            });
        glfwSetFramebufferSizeCallback(Window, [](GLFWwindow* window, int width, int height)
            {
                auto* self = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
                self->Renderer->OnFramebufferResized();
            });

        SetVSync(true);

        ASSERT(glfwVulkanSupported() && "Currently only Vulkan is supported.");
        Renderer = Nyx::CreateRenderer();
        Renderer->Initialize(Data.Title.c_str(), Window);
    }

    void WindowsWindow::Shutdown()
    {
        if (Renderer)
        {
            Renderer->WaitIdle();
        }

        glfwDestroyWindow(Window);
        glfwTerminate();
    }

    void WindowsWindow::ApplyRoundedCorners(GLFWwindow * window)
    {
        ASSERT(window != nullptr);
        HWND hwnd = glfwGetWin32Window(window);
        if (!hwnd)
            return;

        const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
        const HRESULT hr = DwmSetWindowAttribute(
            hwnd,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &pref,
            sizeof(pref)
        );

        // optional: log hr if failed
    }

    void WindowsWindow::DrawUserInterface()
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

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, bIsMaximized ? 0.0f : 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Theme::background);

        ImGui::Begin("DockSpaceWindow", nullptr, windowFlags);

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        const float titlebarHeight = Data.bCustomTitlebar ? 32.0f : 0.0f;
        if (Data.bCustomTitlebar)
        {
            DrawTitlebar(titlebarHeight);

            // @todo LP: Figure out where the padding between titlebar-drag-zone and dockarea comes from
            const float paddingToRemoveHACK = 8.0f;
            // Reserve space so docked content starts below the custom titlebar
            ImGui::Dummy(ImVec2(0.0f, titlebarHeight - paddingToRemoveHACK));
        }

        ImGuiStyle& style = ImGui::GetStyle();
        const float prevMinWinSizeX = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;
        ImGui::DockSpace(ImGui::GetID("MyDockspace"));
        style.WindowMinSize.x = prevMinWinSizeX;

        // Draw the colored border last, so it's on top of everything else
        if (bColoredBorderOnFocus && bAppFocused)
        {
            ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

            const ImVec2 winMin = ImGui::GetWindowPos();
            const ImVec2 winMax(winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight());

            // rounding to match that of win11 windows
            const float rounding = bIsMaximized ? 0.0f : 10.0f;
            // thickness to match the look of a focussed visual studio window
            const float borderThickness = 3.0f;

            constexpr ImU32 borderColor = Colors::Theme::windowBorderER_1;

            // Optional debug: outer frame outline
            drawList->AddRect(
                winMin,
                winMax,
                borderColor,
                rounding,
                0,
                borderThickness
            );

            //// @todo: do we want more titlebar-framing?
            //if (Data.bCustomTitlebar)
            //{
            //    drawList->AddLine(
            //        ImVec2(winMin.x, winMin.y + titlebarHeight),
            //        ImVec2(winMax.x, winMin.y + titlebarHeight),
            //        borderColor,
            //        1.0f
            //    );
            //}
        }

        ImGui::End(); // DockSpaceWindow
    }

    void WindowsWindow::DrawTitlebar(float titlebarHeight)
    {
        const ImVec2 cursorBackup = ImGui::GetCursorPos();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

        const bool bIsMaximized = IsMaximized();

        const ImVec2 titlebarMin = ImVec2(viewport->Pos.x, viewport->Pos.y);
        const ImVec2 titlebarMax = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + titlebarHeight);

        // Background
        drawList->AddRectFilled(titlebarMin, titlebarMax, Colors::Theme::titlebar);

        // Debug entire titlebar area
        //drawList->AddRect(titlebarMin, titlebarMax, IM_COL32(255, 255, 0, 255));

        // Title
        const ImVec2 textSize = ImGui::CalcTextSize(Data.Title.c_str());
        const ImVec2 textPos(
            titlebarMin.x + (viewport->Size.x - textSize.x) * 0.5f,
            titlebarMin.y + (titlebarHeight - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), Data.Title.c_str());

        // Button layout
        const ImVec2 buttonSize(46.0f, titlebarHeight);
        const float rightEdge = titlebarMax.x;

        const ImVec2 closePos(rightEdge - buttonSize.x, titlebarMin.y);
        const ImVec2 maxPos(rightEdge - buttonSize.x * 2.0f, titlebarMin.y);
        const ImVec2 minPos(rightEdge - buttonSize.x * 3.0f, titlebarMin.y);

        // Button colors
        const ImU32 transparent = IM_COL32(0, 0, 0, 0);
        const ImU32 hoverBg = IM_COL32(255, 255, 255, 20);
        const ImU32 pressBg = IM_COL32(255, 255, 255, 35);

        const ImU32 closeHoverBg = IM_COL32(196, 43, 28, 255);
        const ImU32 closePressBg = IM_COL32(160, 35, 24, 255);

        const ImU32 iconColor = IM_COL32(230, 230, 230, 255);

        // Minimize
        if (Icon::Util::DrawTitlebarButton(
            drawList,
            "##TitlebarMinimize",
            Icon::Util::ETitlebarButton::Minimize,
            minPos,
            buttonSize,
            transparent,
            hoverBg,
            pressBg,
            iconColor))
        {
            glfwIconifyWindow(Window);
        }

        // Maximize / Restore
        if (Icon::Util::DrawTitlebarButton(
            drawList,
            "##TitlebarMaxRestore",
            bIsMaximized ? Icon::Util::ETitlebarButton::Restore : Icon::Util::ETitlebarButton::Maximize,
            maxPos,
            buttonSize,
            transparent,
            hoverBg,
            pressBg,
            iconColor))
        {
            if (bIsMaximized)
                glfwRestoreWindow(Window);
            else
                glfwMaximizeWindow(Window);
        }

        // Close
        if (Icon::Util::DrawTitlebarButton(
            drawList,
            "##TitlebarClose",
            Icon::Util::ETitlebarButton::Close,
            closePos,
            buttonSize,
            transparent,
            closeHoverBg,
            closePressBg,
            iconColor))
        {
            glfwSetWindowShouldClose(Window, GLFW_TRUE);
        }

        // Drag region = full titlebar except button area
        const ImVec2 dragMin = titlebarMin;
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

    void WindowsWindow::DrawMenubar()
    {
        const ImRect menuBarRect = { ImGui::GetCursorPos(), { ImGui::GetContentRegionAvail().x + ImGui::GetCursorScreenPos().x, ImGui::GetFrameHeightWithSpacing() } };

        auto beginMenubar = [](const ImRect& barRectangle)
            {
                auto rectOffset = [](const ImRect& rect, float x, float y) -> ImRect
                    {
                        ImRect result = rect;
                        result.Min.x += x;
                        result.Min.y += y;
                        result.Max.x += x;
                        result.Max.y += y;
                        return result;
                    };

                ImGuiWindow* window = ImGui::GetCurrentWindow();
                if (window->SkipItems)
                    return false;
                /*if (!(window->Flags & ImGuiWindowFlags_MenuBar))
                    return false;*/

                IM_ASSERT(!window->DC.MenuBarAppending);
                ImGui::BeginGroup(); // Backup position on layer 0 // FIXME: Misleading to use a group for that backup/restore
                ImGui::PushID("##menubar");

                const ImVec2 padding = window->WindowPadding;

                // We don't clip with current window clipping rectangle as it is already set to the area below. However we clip with window full rect.
                // We remove 1 worth of rounding to Max.x to that text in long menus and small windows don't tend to display over the lower-right rounded area, which looks particularly glitchy.
                ImRect bar_rect = rectOffset(barRectangle, 0.0f, padding.y);// window->MenuBarRect();
                ImRect clip_rect(IM_ROUND(ImMax(window->Pos.x, bar_rect.Min.x + window->WindowBorderSize + window->Pos.x - 10.0f)), IM_ROUND(bar_rect.Min.y + window->WindowBorderSize + window->Pos.y),
                    IM_ROUND(ImMax(bar_rect.Min.x + window->Pos.x, bar_rect.Max.x - ImMax(window->WindowRounding, window->WindowBorderSize))), IM_ROUND(bar_rect.Max.y + window->Pos.y));

                clip_rect.ClipWith(window->OuterRectClipped);
                ImGui::PushClipRect(clip_rect.Min, clip_rect.Max, false);

                // We overwrite CursorMaxPos because BeginGroup sets it to CursorPos (essentially the .EmitItem hack in EndMenuBar() would need something analogous here, maybe a BeginGroupEx() with flags).
                window->DC.CursorPos = window->DC.CursorMaxPos = ImVec2(bar_rect.Min.x + window->Pos.x, bar_rect.Min.y + window->Pos.y);
                window->DC.LayoutType = ImGuiLayoutType_Horizontal;
                window->DC.NavLayerCurrent = ImGuiNavLayer_Menu;
                window->DC.MenuBarAppending = true;
                ImGui::AlignTextToFramePadding();
                return true;
            };

        auto endMenubar = []()
            {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                if (window->SkipItems)
                    return;
                ImGuiContext& g = *GImGui;

                // Nav: When a move request within one of our child menu failed, capture the request to navigate among our siblings.
                if (ImGui::NavMoveRequestButNoResultYet() && (g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right) && (g.NavWindow->Flags & ImGuiWindowFlags_ChildMenu))
                {
                    // Try to find out if the request is for one of our child menu
                    ImGuiWindow* nav_earliest_child = g.NavWindow;
                    while (nav_earliest_child->ParentWindow && (nav_earliest_child->ParentWindow->Flags & ImGuiWindowFlags_ChildMenu))
                        nav_earliest_child = nav_earliest_child->ParentWindow;
                    if (nav_earliest_child->ParentWindow == window && nav_earliest_child->DC.ParentLayoutType == ImGuiLayoutType_Horizontal && (g.NavMoveFlags & ImGuiNavMoveFlags_Forwarded) == 0)
                    {
                        // To do so we claim focus back, restore NavId and then process the movement request for yet another frame.
                        // This involve a one-frame delay which isn't very problematic in this situation. We could remove it by scoring in advance for multiple window (probably not worth bothering)
                        const ImGuiNavLayer layer = ImGuiNavLayer_Menu;
                        IM_ASSERT(window->DC.NavLayersActiveMaskNext & (1 << layer)); // Sanity check
                        ImGui::FocusWindow(window);
                        ImGui::SetNavID(window->NavLastIds[layer], layer, 0, window->NavRectRel[layer]);
                        g.NavCursorVisible = false; // Hide highlight for the current frame so we don't see the intermediary selection.
                        g.NavHighlightItemUnderNav = g.NavMousePosDirty = true;
                        ImGui::NavMoveRequestForward(g.NavMoveDir, g.NavMoveClipDir, g.NavMoveFlags, g.NavMoveScrollFlags); // Repeat
                    }
                }

                IM_MSVC_WARNING_SUPPRESS(6011); // Static Analysis false positive "warning C6011: Dereferencing NULL pointer 'window'"
                // IM_ASSERT(window->Flags & ImGuiWindowFlags_MenuBar); // NOTE(Yan): Needs to be commented out because Jay
                IM_ASSERT(window->DC.MenuBarAppending);
                ImGui::PopClipRect();
                ImGui::PopID();
                window->DC.MenuBarOffset.x = window->DC.CursorPos.x - window->Pos.x; // Save horizontal position so next append can reuse it. This is kinda equivalent to a per-layer CursorPos.
                g.GroupStack.back().EmitItem = false;
                ImGui::EndGroup(); // Restore position on layer 0
                window->DC.LayoutType = ImGuiLayoutType_Vertical;
                window->DC.NavLayerCurrent = ImGuiNavLayer_Main;
                window->DC.MenuBarAppending = false;
            };

        ImGui::BeginGroup();
        if (beginMenubar(menuBarRect))
        {
            //m_MenubarCallback();
        }

        endMenubar();
        ImGui::EndGroup();
    }

    bool WindowsWindow::IsMaximized() const
    {
        return static_cast<bool>(glfwGetWindowAttrib(Window, GLFW_MAXIMIZED));
    }
}

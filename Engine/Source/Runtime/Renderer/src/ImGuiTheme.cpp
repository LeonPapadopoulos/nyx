#include "NyxPCH.h"
#include "ImGuiTheme.h"

#include "UITheme.h"

namespace Nyx
{
	void ApplyThemeToImGui()
	{
		auto& colors = ImGui::GetStyle().Colors;

		//========================================================
		/// Colours: from the painted widgets' theme, so that the text and the widgets ImGui still
		/// draws (its debug tools, and everything with Debug > Painted Chrome off) match them. With
		/// the chrome on, it clears the window, tab and scrollbar colours while it paints them.
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const auto color = [](const glm::vec4& value)
		{
			return ImVec4(value.r, value.g, value.b, value.a);
		};
		const auto opaque = [](glm::vec4 value)
		{
			return ImVec4(value.r, value.g, value.b, 1.0f);
		};
		const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);

		// Text
		colors[ImGuiCol_Text] = color(theme.Text);
		colors[ImGuiCol_TextDisabled] = color(theme.TextMuted);
		colors[ImGuiCol_TextSelectedBg] = color(theme.TextSelection);
		colors[ImGuiCol_InputTextCursor] = color(theme.TextCursor);
		colors[ImGuiCol_TextLink] = color(theme.AccentHovered);

		// Headers, selectables and tree nodes
		colors[ImGuiCol_Header] = color(theme.Frame);
		colors[ImGuiCol_HeaderHovered] = color(theme.FrameHovered);
		colors[ImGuiCol_HeaderActive] = color(theme.RowSelected);

		// Buttons
		colors[ImGuiCol_Button] = color(theme.Frame);
		colors[ImGuiCol_ButtonHovered] = color(theme.FrameHovered);
		colors[ImGuiCol_ButtonActive] = color(theme.FramePressed);

		// Fields
		colors[ImGuiCol_FrameBg] = color(theme.Field);
		colors[ImGuiCol_FrameBgHovered] = color(theme.FieldHovered);
		colors[ImGuiCol_FrameBgActive] = color(theme.FieldHovered);

		// Tabs
		colors[ImGuiCol_Tab] = clear;
		colors[ImGuiCol_TabHovered] = color(theme.TabHovered);
		colors[ImGuiCol_TabSelected] = color(theme.TabSelected);
		colors[ImGuiCol_TabSelectedOverline] = color(theme.TabUnderline);
		colors[ImGuiCol_TabDimmed] = clear;
		colors[ImGuiCol_TabDimmedSelected] = color(theme.TabSelected);
		colors[ImGuiCol_TabDimmedSelectedOverline] = color(theme.TabUnderlineDimmed);

		// Titles
		colors[ImGuiCol_TitleBg] = color(theme.Titlebar);
		colors[ImGuiCol_TitleBgActive] = color(theme.Titlebar);
		colors[ImGuiCol_TitleBgCollapsed] = color(theme.Titlebar);

		// Resize grips
		colors[ImGuiCol_ResizeGrip] = color(theme.Separator);
		colors[ImGuiCol_ResizeGripHovered] = color(theme.Splitter);
		colors[ImGuiCol_ResizeGripActive] = color(theme.Accent);

		// Scrollbars
		colors[ImGuiCol_ScrollbarBg] = clear;
		colors[ImGuiCol_ScrollbarGrab] = color(theme.ScrollbarThumb);
		colors[ImGuiCol_ScrollbarGrabHovered] = color(theme.ScrollbarThumbHovered);
		colors[ImGuiCol_ScrollbarGrabActive] = color(theme.ScrollbarThumbHeld);

		// Check marks and sliders
		colors[ImGuiCol_CheckMark] = color(theme.Accent);
		colors[ImGuiCol_SliderGrab] = color(theme.Accent);
		colors[ImGuiCol_SliderGrabActive] = color(theme.AccentHovered);

		// Separators and splitters
		colors[ImGuiCol_Separator] = color(theme.Separator);
		colors[ImGuiCol_SeparatorHovered] = color(theme.Splitter);
		colors[ImGuiCol_SeparatorActive] = color(theme.Accent);

		// Windows
		colors[ImGuiCol_WindowBg] = opaque(theme.Card);
		colors[ImGuiCol_ChildBg] = clear;
		colors[ImGuiCol_PopupBg] = color(theme.FloatingCard);
		colors[ImGuiCol_Border] = color(theme.CardBorder);
		colors[ImGuiCol_MenuBarBg] = clear;

		// Docking
		colors[ImGuiCol_DockingPreview] = ImVec4(theme.Accent.r, theme.Accent.g, theme.Accent.b, 0.45f);
		colors[ImGuiCol_DockingEmptyBg] = color(theme.Backdrop);

		// Tables
		colors[ImGuiCol_TableHeaderBg] = color(theme.TableHeader);
		colors[ImGuiCol_TableBorderStrong] = color(theme.Separator);
		colors[ImGuiCol_TableBorderLight] = color(theme.Separator);
		colors[ImGuiCol_TableRowBg] = clear;
		colors[ImGuiCol_TableRowBgAlt] = color(theme.TableRowStripe);

		// Keyboard navigation and drag and drop
		colors[ImGuiCol_NavCursor] = color(theme.Accent);
		colors[ImGuiCol_DragDropTarget] = color(theme.Accent);
	}

	void SetNyxTheme()
	{
		ApplyThemeToImGui();

		auto& style = ImGui::GetStyle();

		//========================================================
		/// Style
		style.FrameRounding = 2.5f;
		style.FrameBorderSize = 1.0f;
		style.IndentSpacing = 11.0f;
	}
}

#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <string_view>

// Painted widgets: drawn with the window's painter (Nyx::UI::GetWindowPainter()) instead of
// ImDrawList, so their edges, shadows and glows are crisp at any size. The plan is in
// Design/ui-painter.md, stage 2, change 5.
//
// They behave exactly like ImGui's own: they are built on ImGui's behaviour functions
// (ItemSize(), ItemAdd(), ButtonBehavior(), SliderBehavior()), so IDs, mouse and keyboard
// navigation, BeginDisabled() and the last-item queries (IsItemHovered() and so on) work as for
// any ImGui widget, and NYX_UI() wraps them like one:
//
//     if (NYX_UI(Nyx::UI::Button("Save")))
//     {
//         ...
//     }
//
// Each widget is two parts: its behaviour, which works out a WidgetLook from the mouse and the
// keyboard, and a Paint...() function that paints any WidgetLook. The UI gallery (the Painter
// Test window's Widgets tab) calls the Paint...() functions directly, to show every state
// without the mouse.
//
// Looks come from Nyx::UI::GetTheme(); layout from ImGui's style, so painted widgets line up
// with ImGui's. Labels work as ImGui's: the text after "##" is part of the ID only.

namespace Nyx::UI
{
	class WindowPainter;

	// How a widget looks at one moment: how hovered, pressed and keyboard-focused it is, from 0
	// to 1 (eased over frames, Animate()), and whether it is disabled
	struct WidgetLook
	{
		float Hovered = 0.0f;
		float Pressed = 0.0f;
		float Focused = 0.0f;
		bool bDisabled = false;
	};

	// The widgets. Each returns what its ImGui counterpart returns.

	// A button with a label; a size of 0 on an axis fits the label. True when clicked.
	bool Button(const char* label, const glm::vec2& size = glm::vec2(0.0f));

	// A switch, with its label to the right; it flips `value` when clicked. True when it flipped.
	bool Toggle(const char* label, bool* value);

	// A slider from `min` to `max`, with its value as text by `format` (printf) beside the track,
	// and its label to the right. True when the value changed.
	bool SliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f");

	// A heading over a section: an accent bar, the label, and a line to the window's edge
	void SectionHeader(const char* label);

	// What a tree row shows left of its label
	enum class ETreeRowIcon
	{
		None,
		Folder,
		Entity,
	};

	struct TreeRowOptions
	{
		bool bSelected = false;
		// Without children: no arrow, never open, and nothing to end
		bool bLeaf = false;
		// Open the first time the row shows
		bool bDefaultOpen = false;
		ETreeRowIcon Icon = ETreeRowIcon::None;
	};

	struct TreeRowResult
	{
		// The row was clicked, or activated by keyboard, to select it; not when its arrow was
		bool bClicked = false;
		// Its children follow, indented, until TreeRowEnd()
		bool bOpen = false;
	};

	// One row of a tree or a list, as the outliner's entities and the asset browser's folders
	// show them (stage 3, change 7a): a pill behind it when selected or hovered, guides down
	// each indentation level, an arrow that turns as it opens, an icon and the label. The row
	// and its pill span the window's width at any indentation.
	//
	// Behaves like ImGui::TreeNodeEx() with OpenOnArrow: the arrow, a double click, or the Left
	// and Right keys open and close it, and the rest of the row selects it. The open state is
	// kept by ImGui, per ID. IsItemHovered(), IsItemClicked(ImGuiMouseButton_Right) and the like
	// work after it.
	TreeRowResult TreeRow(const char* label, const TreeRowOptions& options = TreeRowOptions{});

	// Ends the children of a row that was open, as ImGui::TreePop() does
	void TreeRowEnd();

	// The painting, for any look. Rects are in ImGui's screen coordinates, as the widgets lay
	// themselves out: the widget's whole rect, label included, except the slider's, which is its
	// frame. Labels are what is shown, without "##".

	void PaintButton(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, const WidgetLook& look);
	// `on`: the knob's way from off (0) to on (1)
	void PaintToggle(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, float on, const WidgetLook& look);
	// `fraction`: the value's way from min (0) to max (1). `valueWidth`: room for the value's
	// text, the widest it gets (CalcSliderValueWidth()), so the track doesn't change length
	// with the value.
	void PaintSlider(WindowPainter& painter, const Engine::PainterRect& frame, std::string_view label, float fraction, std::string_view valueText, float valueWidth, const WidgetLook& look);
	void PaintSectionHeader(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label);

	// How a tree row looks besides its WidgetLook
	struct TreeRowLook
	{
		// Where the row's indentation ends and its arrow starts
		float ContentX = 0.0f;
		// How many rows it is inside: one guide each
		int Depth = 0;
		bool bLeaf = false;
		// The arrow's turn, from closed (0, pointing right) to open (1, pointing down)
		float Open = 0.0f;
		// The selection pill, faded in from 0 to 1
		float Selected = 0.0f;
		ETreeRowIcon Icon = ETreeRowIcon::None;
	};
	// `rect`: the whole row, the window's width
	void PaintTreeRow(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, const TreeRowLook& row, const WidgetLook& look);

	// The sizes the widgets take, at the current font, for the gallery's forced states
	glm::vec2 CalcButtonSize(const char* label);
	glm::vec2 CalcToggleSize(const char* label);
	// The slider's frame, ImGui's item width wide
	glm::vec2 CalcSliderFrameSize();
	float CalcSliderValueWidth(float min, float max, const char* format);
	glm::vec2 CalcSectionHeaderSize();
	float CalcTreeRowHeight();
}

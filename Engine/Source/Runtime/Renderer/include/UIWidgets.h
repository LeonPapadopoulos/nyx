#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

// Painted widgets: drawn with the window's painter (Nyx::UI::GetWindowPainter()) instead of
// ImDrawList, so their edges, shadows and glows are crisp at any size. The plan is in
// Design/ui-painter.md, stage 2, change 5, and stage 3, change 7.
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
// Fields are hybrids instead: ImGui's own text and number fields, which keep their editing and
// their text, over a painted frame (InputText() and below). So are menu items and selectables:
// ImGui's own, drawn invisible, and painted after them (MenuItem() and below).
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
		// A click anywhere on the row, or activating it by keyboard, opens and closes it, as for
		// a heading in a list (the details panel's categories and structs). It never reports
		// bClicked then.
		bool bToggleOnClick = false;
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

	// A heading that opens and closes what follows it, e.g. a component in the details panel:
	// a pill across the window, an arrow that turns as it opens, and the label. Behaves like
	// ImGui::CollapsingHeader(), and keeps its open state where that does, so a header keeps
	// its state when moved from one to the other. Indents nothing. True while open.
	bool CollapsingHeader(const char* label, bool bDefaultOpen = true);

	// Fields (stage 3, change 7b): ImGui's own text and number fields over a painted frame.
	// ImGui keeps the editing, the dragging and the text; its frame colours are made transparent
	// while it draws them, so only the painted frame shows. Each returns what its ImGui
	// counterpart does, and IsItemActivated(), IsItemDeactivatedAfterEdit() and the like work
	// after it.
	//
	// The frame is painted before ImGui's widget runs, so that the text lies on top of it. ImGui
	// knows whether a field is hovered only once the widget has run, so the frame shows the
	// hover of the frame before; eased over frames, the delay doesn't show.

	bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags = 0);

	// A text field with round ends and a magnifier, showing `placeholder` while it is empty
	bool SearchField(const char* label, std::string* value, const char* placeholder = "Search");

	// A number dragged with the mouse, or typed after a double click or Ctrl+click. `format`:
	// printf's, or null for the type's own.
	bool DragScalar(const char* label, ImGuiDataType type, void* value, float speed, const char* format = nullptr);
	bool DragFloat(const char* label, float* value, float speed, const char* format = "%.3f");

	// A number that is only typed, never dragged, e.g. an ID in hex
	// (ImGuiInputTextFlags_CharsHexadecimal)
	bool InputScalar(const char* label, ImGuiDataType type, void* value, const char* format = nullptr, ImGuiInputTextFlags flags = 0);

	// `count` floats (1 to 4) side by side, sharing the item width: each a DragFloat() behind its
	// axis letter, X, Y, Z and W in the theme's colours. Like ImGui::DragFloat3(), they are one
	// group, so IsItemActivated() and IsItemDeactivatedAfterEdit() after it cover all of them.
	bool DragVector(const char* label, float* values, int count, float speed, const char* format = "%.3f");

	// A property row, as the details panel shows each property: the label in a column on the
	// left, and the value, which the caller draws between BeginPropertyRow() and
	// EndPropertyRow(), on the right. The value's item width is set to the rest of the row.
	//
	// The row spans the window's width, with the tree rows' hover pill and guides; inside an
	// open tree row its label is indented, and lines up with the tree rows' labels. The value
	// column starts at the same place at any indentation. After BeginPropertyRow(),
	// IsItemHovered() tells whether the label is hovered, e.g. for a tooltip.
	void BeginPropertyRow(const char* label);
	void EndPropertyRow();

	// A box, ticked when `value` is true, with its label to the right; it flips `value` when
	// clicked. True when it flipped, like ImGui::Checkbox().
	bool Checkbox(const char* label, bool* value);

	// Menus and lists (stage 3, change 7c). Menu items and selectables are ImGui's own, which keep
	// what makes them work in menus and popups (closing the popup on a click, keyboard
	// navigation, hovering to move the keyboard's place), but drawn invisible: their highlight
	// and text are transparent while ImGui draws them. They are painted after ImGui's widget has
	// run, with the state ImGui has then, at the rect it laid out.

	// An item of a menu: the label on the left, the shortcut's text right-aligned, and a tick on
	// the right when selected. True when clicked; that closes the menu. In a menu bar, ImGui's own.
	bool MenuItem(const char* label, const char* shortcut = nullptr, bool bSelected = false, bool bEnabled = true);
	// As above, flipping `selected` when clicked
	bool MenuItem(const char* label, const char* shortcut, bool* selected, bool bEnabled = true);

	// A line of a list that can be selected, as ImGui::Selectable(): the selection pill behind it
	// when `bSelected`, the hover pill when hovered. In a popup, a click closes it.
	bool Selectable(const char* label, bool bSelected = false, ImGuiSelectableFlags flags = 0, const glm::vec2& size = glm::vec2(0.0f));

	// A thin line across the window, as ImGui::Separator(); in a menu bar, ImGui's own
	void Separator();

	// A combo box: a frame with the chosen item's text and a chevron, which turns as the combo
	// opens. Its list is ImGui's popup, a floating card with the chrome on; fill it with
	// Selectable()s and end it with EndCombo() when BeginCombo() returned true, as with ImGui's.
	bool BeginCombo(const char* label, const char* preview);
	void EndCombo();
	// A combo of `items`, choosing `current`. True when another item was chosen.
	bool Combo(const char* label, int* current, std::span<const char* const> items);

	struct ColorSwatchResult
	{
		// The colour changed this frame
		bool bChanged = false;
		// An edit in the picker started or ended this frame, e.g. a drag in it: for undo, as
		// IsItemActivated() and IsItemDeactivatedAfterEdit() are for other fields
		bool bEditStarted = false;
		bool bEditEnded = false;
	};

	// A colour as a swatch, the item width wide, with its label to the right. A click opens
	// ImGui's colour picker in a popup under it, which changes `rgb` (3 floats, 0 to 1).
	ColorSwatchResult ColorSwatch(const char* label, float* rgb);

	// Tables (stage 3, change 7d): ImGui's tables, which keep their layout, column sizing,
	// scrolling, clipping and headers' behaviour, with their backgrounds and borders made
	// transparent. Painted instead: a strip behind the header row with separators between its
	// cells, rounded stripes on every other row (ImGuiTableFlags_RowBg), and an accent line on a
	// column border being hovered or dragged. Each row is painted when it ends, into the table's
	// background channel, so it lies behind the cells.
	//
	// Use these in place of ImGui's; the rest of ImGui's table API (TableSetupColumn(),
	// TableNextColumn(), TableSetupScrollFreeze()) is used as usual. With an ImGuiListClipper,
	// step it with TableClipperStep(): the clipper ends rows itself, and a row must be painted
	// before that. A Selectable() with ImGuiSelectableFlags_SpanAllColumns paints its pills across
	// the row.
	bool BeginTable(const char* id, int columns, ImGuiTableFlags flags = 0, const glm::vec2& outerSize = glm::vec2(0.0f), float innerWidth = 0.0f);
	void TableNextRow(ImGuiTableRowFlags flags = 0, float minHeight = 0.0f);
	void TableHeadersRow();
	bool TableClipperStep(ImGuiListClipper& clipper);
	void EndTable();

	// What an asset card shows when it has no thumbnail
	enum class EAssetCardIcon
	{
		Folder,
		File,
	};

	struct AssetCardOptions
	{
		bool bSelected = false;
		EAssetCardIcon Icon = EAssetCardIcon::File;
		// Shown under a file's icon, e.g. the asset's type
		std::string_view TypeLabel;
		// The asset's picture, shown instead of the icon when set
		ImTextureRef Thumbnail;
	};

	struct AssetCardResult
	{
		// Clicked, to select it
		bool bClicked = false;
		// Double-clicked, or activated by keyboard, to open it
		bool bOpened = false;
	};

	// One asset in a grid of them, as the asset browser shows them: a picture (the thumbnail, or
	// a painted folder or page with the type under it), and the name under it, cut to fit. Lay
	// cards out side by side with SameLine() while they fit, CalcAssetCardSize() each.
	AssetCardResult AssetCard(const char* label, const AssetCardOptions& options = AssetCardOptions{});

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
	// `open`: the arrow's turn, from closed (0) to open (1)
	void PaintCollapsingHeader(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, float open, const WidgetLook& look);

	// The fields' frames, without their text, which ImGui draws. `editing`: how much the field
	// is being typed in or dragged, from 0 to 1.
	void PaintField(WindowPainter& painter, const Engine::PainterRect& frame, float editing, const WidgetLook& look);
	void PaintSearchField(WindowPainter& painter, const Engine::PainterRect& frame, float editing, const WidgetLook& look);
	// One component of a vector field: its frame, with the letter of `axis` (0 for X to 3 for W)
	// at the left
	void PaintVectorComponent(WindowPainter& painter, const Engine::PainterRect& frame, int axis, float editing, const WidgetLook& look);

	// How a property row looks besides its WidgetLook, as for a tree row
	struct PropertyRowLook
	{
		// Where the row's indentation ends
		float ContentX = 0.0f;
		// How many tree rows it is inside: one guide each
		int Depth = 0;
	};
	// `rect`: the whole row, the window's width. Paints the hover pill, the guides and the label.
	void PaintPropertyRow(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, const PropertyRowLook& row, const WidgetLook& look);

	// `on`: the tick's way in, from off (0) to on (1)
	void PaintCheckbox(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, float on, const WidgetLook& look);
	// `rect`: the item as ImGui lays it out, across the menu, half the item spacing larger than its text all round
	void PaintMenuItem(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, std::string_view shortcut, bool bChecked, const WidgetLook& look);
	// `rect`: as for a menu item. `selected`: the selection pill, faded in from 0 to 1.
	void PaintSelectable(WindowPainter& painter, const Engine::PainterRect& rect, glm::vec2 labelPosition, std::string_view label, float selected, const WidgetLook& look);
	// `open`: the chevron's turn, from closed (0, pointing down) to open (1, pointing up)
	void PaintCombo(WindowPainter& painter, const Engine::PainterRect& frame, std::string_view preview, float open, const WidgetLook& look);
	void PaintColorSwatch(WindowPainter& painter, const Engine::PainterRect& frame, const glm::vec3& color, const WidgetLook& look);
	// A tick in `box`, `amount` of the way drawn (0 to 1)
	void PaintCheckmark(WindowPainter& painter, const Engine::PainterRect& box, float amount, const glm::vec4& color);

	// How an asset card looks besides its WidgetLook
	struct AssetCardLook
	{
		EAssetCardIcon Icon = EAssetCardIcon::File;
		std::string_view TypeLabel;
		ImTextureRef Thumbnail;
		// The selection, faded in from 0 to 1
		float Selected = 0.0f;
	};
	void PaintAssetCard(WindowPainter& painter, const Engine::PainterRect& rect, std::string_view label, const AssetCardLook& card, const WidgetLook& look);

	// The sizes the widgets take, at the current font, for the gallery's forced states
	glm::vec2 CalcButtonSize(const char* label);
	glm::vec2 CalcToggleSize(const char* label);
	// The slider's frame, ImGui's item width wide
	glm::vec2 CalcSliderFrameSize();
	float CalcSliderValueWidth(float min, float max, const char* format);
	glm::vec2 CalcSectionHeaderSize();
	float CalcTreeRowHeight();
	// The room a vector field's axis letter takes at the left of each component
	float CalcAxisTagWidth();
	// Where a property row's value column starts, for a row from `rowMinX` to `rowMaxX`
	float CalcPropertyValueX(float rowMinX, float rowMaxX);
	// Where a search field's text starts, from its frame's left
	float CalcSearchTextOffset();
	glm::vec2 CalcCheckboxSize(const char* label);
	glm::vec2 CalcAssetCardSize();
}

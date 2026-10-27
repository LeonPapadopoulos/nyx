#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

// Painted widgets: drawn with the window's painter instead of ImDrawList, so their edges, shadows
// and glows are crisp at any size. They are built on ImGui's behaviour functions, so IDs,
// navigation, BeginDisabled() and the last-item queries work as for any ImGui widget, and NYX_UI()
// wraps them like one. Each widget's behaviour works out a WidgetLook, and a Paint...() function
// paints any WidgetLook, so every state can be shown without the mouse.

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

	// What a button does, which picks its colour: Go starts something (Play), Stop ends it, and
	// Primary does the main thing of its bar (Build), or shows that a toggle is on (Pause)
	enum class EButtonTone
	{
		Neutral,
		Go,
		Stop,
		Primary,
	};

	// A button with a label; a size of 0 on an axis fits the label. True when clicked.
	bool Button(const char* label, const glm::vec2& size = glm::vec2(0.0f), EButtonTone tone = EButtonTone::Neutral);

	// A button for a menu bar, e.g. File in the titlebar; `bOpen` while its menu is open. True when
	// clicked, to open the menu.
	bool MenuBarButton(const char* label, const glm::vec2& size, bool bOpen);

	// The buttons of a window's titlebar that Windows would draw
	enum class EWindowControl
	{
		Minimize,
		Maximize,
		Restore,
		Close,
	};

	// A window control `size` large; `id` as a label after "##". True when clicked.
	bool WindowControlButton(const char* id, EWindowControl control, const glm::vec2& size);

	// Icons painted from shapes. All but Play, Stop, Pause and Step are placeholders until there is
	// an icon font.
	enum class EIcon
	{
		None,
		Play,
		// Play in several games
		PlayAll,
		Stop,
		Pause,
		Step,
		Build,
		Document,
		// Picks something by clicking it, e.g. an entity in a scene view
		Eyedropper,
		// Goes to what a field names, e.g. selects the entity
		Locate,
		// Puts a value back to what it was given, e.g. takes an override away
		Reset,
		// Empties a field, e.g. a search
		Clear,
	};

	// A button with an icon, alone (a label of "##Play") or before its label; a size of 0 on an
	// axis fits them. bDropdown marks it as opening a menu. True when clicked.
	bool IconButton(const char* label, EIcon icon, const glm::vec2& size = glm::vec2(0.0f), EButtonTone tone = EButtonTone::Neutral, bool bDropdown = false);

	// A tab of a bar, e.g. a workspace. True when clicked.
	bool BarTab(const char* label, bool bSelected, const glm::vec2& size);

	struct DocumentTabResult
	{
		bool bClicked = false;
		bool bCloseClicked = false;
	};

	// An open document's tab in a bar, e.g. a scene's, with a close button, disabled without
	// bCanClose.
	// The size's height is the tab's; a width of 0 fits the name.
	DocumentTabResult DocumentTab(const char* label, bool bCanClose, const glm::vec2& size);

	// A thin upright line between groups of a bar, BarDividerHeight of `height` tall and centred
	// on it, with ImGui's item spacing on both sides
	void BarDivider(float height);

	// What a status bar item's icon says
	enum class EStatusIcon
	{
		None,
		Ok,
		Warning,
		Error,
	};

	// An item of a status bar: an icon, if any, and text, as tall as `height`. IsItemHovered()
	// works after it, e.g. for a tooltip.
	void StatusItem(const char* text, EStatusIcon icon, float height);

	// The application's mark and name, `height` tall
	void TitlebarLogo(const char* name, float height);

	// A switch, with its label to the right; it flips `value` when clicked. True when it flipped.
	bool Toggle(const char* label, bool* value);

	// A slider from `min` to `max`, with its value as text by `format` (printf) beside the track,
	// and its label to the right. True when the value changed. ImGuiSliderFlags_Logarithmic
	// spreads a range above 0 by ratio, as ImGui's does.
	bool SliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0);

	// A heading over a section
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
		// A double click on the label opens and closes it; off for a row whose double click does
		// something else, e.g. renames it
		bool bDoubleClickToggles = true;
		ETreeRowIcon Icon = ETreeRowIcon::None;
	};

	struct TreeRowResult
	{
		// The row was clicked, or activated by keyboard, to select it; not when its arrow was
		bool bClicked = false;
		// Its children follow, indented, until TreeRowEnd()
		bool bOpen = false;
	};

	// One row of a tree or a list, e.g. the outliner's entities. Behaves like ImGui::TreeNodeEx()
	// with OpenOnArrow; ImGui keeps the open state, per ID, ImGui::SetNextItemOpen() sets it, and
	// IsItemHovered() and the like work after it.
	TreeRowResult TreeRow(const char* label, const TreeRowOptions& options = TreeRowOptions{});

	// How far right of where a tree row starts (the cursor before it) its label starts: past the
	// arrow, and the icon, if any. The arrow is CalcTreeRowLabelOffset(ETreeRowIcon::None) wide.
	float CalcTreeRowLabelOffset(ETreeRowIcon icon);

	// Ends the children of a row that was open, as ImGui::TreePop() does
	void TreeRowEnd();

	// A heading that opens and closes what follows it. Behaves like ImGui::CollapsingHeader(), and
	// keeps its open state where that does, so a header keeps its state when moved from one to the
	// other. Indents nothing. True while open.
	bool CollapsingHeader(const char* label, bool bDefaultOpen = true);

	// Fields: ImGui's own text and number fields, which keep their editing and text, over a
	// painted frame. Each returns what its ImGui counterpart does. The frame is painted before
	// ImGui's widget runs, so that the text lies on top of it; ImGui knows whether a field is
	// hovered only once the widget has run, so the frame shows the hover of the frame before,
	// which easing hides.

	// `hint`: shown, muted, while the field is empty
	bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags = 0, const char* hint = nullptr);

	// A field of several lines, `size` large (0 on an axis: the item width, or 8 lines), which
	// scrolls; ImGuiInputTextFlags_ReadOnly for text to select and copy
	bool InputTextMultiline(const char* label, std::string* value, const glm::vec2& size = glm::vec2(0.0f), ImGuiInputTextFlags flags = 0);

	// A text field with a magnifier, showing `placeholder` while it is empty
	bool SearchField(const char* label, std::string* value, const char* placeholder = "Search");

	// A number dragged with the mouse, or typed after a double click or Ctrl+click. `format`:
	// printf's, or null for the type's own.
	bool DragScalar(const char* label, ImGuiDataType type, void* value, float speed, const char* format = nullptr);
	bool DragFloat(const char* label, float* value, float speed, const char* format = "%.3f");

	// A number that is only typed, never dragged, e.g. an ID in hex
	// (ImGuiInputTextFlags_CharsHexadecimal)
	bool InputScalar(const char* label, ImGuiDataType type, void* value, const char* format = nullptr, ImGuiInputTextFlags flags = 0);

	// `count` floats (1 to 4) side by side, sharing the item width. Like ImGui::DragFloat3(), they
	// are one group, so IsItemActivated() and IsItemDeactivatedAfterEdit() after it cover all of
	// them.
	bool DragVector(const char* label, float* values, int count, float speed, const char* format = "%.3f");

	// A property row: the label on the left, and the value, which the caller draws between
	// BeginPropertyRow() and EndPropertyRow(), with the rest of the row as its item width. The
	// value column starts at the same place at any indentation. After BeginPropertyRow(),
	// IsItemHovered() tells whether the label is hovered, e.g. for a tooltip.
	void BeginPropertyRow(const char* label);
	void EndPropertyRow();

	// A checkbox; it flips `value` when clicked. True when it flipped, like ImGui::Checkbox().
	bool Checkbox(const char* label, bool* value);

	// Menus and lists: menu items and selectables are ImGui's own, drawn invisible, so they keep
	// what makes them work in menus and popups (closing the popup on a click, keyboard
	// navigation). They are painted after ImGui's widget has run, with its state and rect.

	// An item of a menu. True when clicked; that closes the menu. In a menu bar, ImGui's own.
	bool MenuItem(const char* label, const char* shortcut = nullptr, bool bSelected = false, bool bEnabled = true);
	// As above, flipping `selected` when clicked
	bool MenuItem(const char* label, const char* shortcut, bool* selected, bool bEnabled = true);

	// A line of a list that can be selected, as ImGui::Selectable(). In a popup, a click closes it.
	bool Selectable(const char* label, bool bSelected = false, ImGuiSelectableFlags flags = 0, const glm::vec2& size = glm::vec2(0.0f));

	// A thin line across the window, as ImGui::Separator(); in a menu bar, ImGui's own
	void Separator();

	// A combo box. Fill its popup with Selectable()s and end it with EndCombo() when BeginCombo()
	// returned true, as with ImGui's.
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

	// A colour swatch; a click opens ImGui's colour picker in a popup, which changes `rgb` (3
	// floats, 0 to 1).
	ColorSwatchResult ColorSwatch(const char* label, float* rgb);

	// Tables: ImGui's tables with their backgrounds and borders painted instead. Use these in place
	// of ImGui's; the rest of ImGui's table API is used as usual. With an ImGuiListClipper, step it
	// with TableClipperStep(): the clipper ends rows itself, and a row must be painted before that.
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

	// One asset in a grid of them, as the asset browser shows them. Lay cards out side by side with
	// SameLine() while they fit, CalcAssetCardSize() each.
	AssetCardResult AssetCard(const char* label, const AssetCardOptions& options = AssetCardOptions{});

	// The painting, for any look. Rects are in ImGui's screen coordinates, as the widgets lay
	// themselves out: the widget's whole rect, label included, except the slider's, which is its
	// frame. Labels are what is shown, without "##".

	void PaintButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, const WidgetLook& look, EButtonTone tone = EButtonTone::Neutral);
	// `open`: the open menu's pill, faded in from 0 to 1
	void PaintMenuBarButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, float open, const WidgetLook& look);
	void PaintWindowControl(WindowPainter& painter, const PainterRect& rect, EWindowControl control, const WidgetLook& look);
	void PaintIconButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, EIcon icon, const WidgetLook& look, EButtonTone tone = EButtonTone::Neutral, bool bDropdown = false);
	// An icon filling `box`, a square
	void PaintIcon(WindowPainter& painter, const PainterRect& box, EIcon icon, const glm::vec4& color);
	// `selected`: the raised tab and underline, faded in from 0 to 1
	void PaintBarTab(WindowPainter& painter, const PainterRect& rect, std::string_view label, float selected, const WidgetLook& look);
	// `closeLook`: the close button's own look; disabled without a way to close
	void PaintDocumentTab(WindowPainter& painter, const PainterRect& rect, std::string_view label, const WidgetLook& look, const WidgetLook& closeLook);
	// `rect`: the item, icon and text, as tall as the bar's content
	void PaintStatusItem(WindowPainter& painter, const PainterRect& rect, std::string_view text, EStatusIcon icon);
	void PaintTitlebarLogo(WindowPainter& painter, const PainterRect& rect, std::string_view name);
	// `on`: the knob's way from off (0) to on (1)
	void PaintToggle(WindowPainter& painter, const PainterRect& rect, std::string_view label, float on, const WidgetLook& look);
	// `fraction`: the value's way from min (0) to max (1). `valueWidth`: room for the value's
	// text, the widest it gets (CalcSliderValueWidth()), so the track doesn't change length
	// with the value.
	void PaintSlider(WindowPainter& painter, const PainterRect& frame, std::string_view label, float fraction, std::string_view valueText, float valueWidth, const WidgetLook& look);
	void PaintSectionHeader(WindowPainter& painter, const PainterRect& rect, std::string_view label);

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
	void PaintTreeRow(WindowPainter& painter, const PainterRect& rect, std::string_view label, const TreeRowLook& row, const WidgetLook& look);
	// `open`: the arrow's turn, from closed (0) to open (1)
	void PaintCollapsingHeader(WindowPainter& painter, const PainterRect& rect, std::string_view label, float open, const WidgetLook& look);

	// The fields' frames, without their text, which ImGui draws. `editing`: how much the field
	// is being typed in or dragged, from 0 to 1.
	void PaintField(WindowPainter& painter, const PainterRect& frame, float editing, const WidgetLook& look);
	void PaintSearchField(WindowPainter& painter, const PainterRect& frame, float editing, const WidgetLook& look);
	// One component of a vector field: its frame, with the letter of `axis` (0 for X to 3 for W)
	// at the left
	void PaintVectorComponent(WindowPainter& painter, const PainterRect& frame, int axis, float editing, const WidgetLook& look);

	// How a property row looks besides its WidgetLook, as for a tree row
	struct PropertyRowLook
	{
		// Where the row's indentation ends
		float ContentX = 0.0f;
		// How many tree rows it is inside: one guide each
		int Depth = 0;
	};
	// `rect`: the whole row, the window's width. Paints the hover pill, the guides and the label.
	void PaintPropertyRow(WindowPainter& painter, const PainterRect& rect, std::string_view label, const PropertyRowLook& row, const WidgetLook& look);

	// `on`: the tick's way in, from off (0) to on (1)
	void PaintCheckbox(WindowPainter& painter, const PainterRect& rect, std::string_view label, float on, const WidgetLook& look);
	// `rect`: the item as ImGui lays it out, across the menu, half the item spacing larger than its
	// text all round
	void PaintMenuItem(WindowPainter& painter, const PainterRect& rect, std::string_view label, std::string_view shortcut, bool bChecked, const WidgetLook& look);
	// `rect`: as for a menu item. `selected`: the selection pill, faded in from 0 to 1.
	void PaintSelectable(WindowPainter& painter, const PainterRect& rect, glm::vec2 labelPosition, std::string_view label, float selected, const WidgetLook& look);
	// `open`: the chevron's turn, from closed (0, pointing down) to open (1, pointing up)
	void PaintCombo(WindowPainter& painter, const PainterRect& frame, std::string_view preview, float open, const WidgetLook& look);
	void PaintColorSwatch(WindowPainter& painter, const PainterRect& frame, const glm::vec3& color, const WidgetLook& look);
	// A tick in `box`, `amount` of the way drawn (0 to 1)
	void PaintCheckmark(WindowPainter& painter, const PainterRect& box, float amount, const glm::vec4& color);

	// How an asset card looks besides its WidgetLook
	struct AssetCardLook
	{
		EAssetCardIcon Icon = EAssetCardIcon::File;
		std::string_view TypeLabel;
		ImTextureRef Thumbnail;
		// The selection, faded in from 0 to 1
		float Selected = 0.0f;
	};
	void PaintAssetCard(WindowPainter& painter, const PainterRect& rect, std::string_view label, const AssetCardLook& card, const WidgetLook& look);

	// The sizes the widgets take, at the current font, for the gallery's forced states
	glm::vec2 CalcButtonSize(const char* label);
	// At the frame height, unless `height` is given
	glm::vec2 CalcIconButtonSize(const char* label, EIcon icon, bool bDropdown, float height = 0.0f);
	glm::vec2 CalcBarTabSize(const char* label, float height);
	glm::vec2 CalcDocumentTabSize(const char* label, float height);
	glm::vec2 CalcStatusItemSize(const char* text, EStatusIcon icon, float height);
	glm::vec2 CalcTitlebarLogoSize(const char* name, float height);
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

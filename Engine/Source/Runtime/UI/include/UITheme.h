#pragma once

#include "Painter.h"

#include <glm/glm.hpp>

namespace Nyx::UI
{
	// How the painted widgets (UIWidgets.h) and the window chrome (UIChrome.h) look. Every colour,
	// radius, size and effect they use comes from here, never from a literal in a widget, so that
	// the whole look can change in one place. The editor fills it from a theme file (UIThemeFile.h,
	// Editor/Themes) and edits it live in its theme window. The values here are the default
	// theme's, Editor/Themes/Default.nyxtheme, which holds the same. ImGui's own colours follow the
	// theme (ApplyThemeToImGui()), for the text and widgets ImGui still draws.
	//
	// Layout (frame padding, item spacing, item width) follows ImGui's style instead, so painted
	// widgets line up with ImGui's in the same window.
	//
	// Colours are straight RGBA, as Painter takes them. Sizes are pixels, or fractions of the
	// frame height (the font size plus the vertical frame padding twice) where they say so, so
	// that those grow with the font.
	struct Theme
	{
		// Text
		glm::vec4 Text{ 0.91f, 0.918f, 0.949f, 1.0f };
		glm::vec4 TextMuted{ 0.616f, 0.639f, 0.675f, 1.0f };
		// Notes that say how to read what is shown, e.g. that vsync holds a frame rate back
		glm::vec4 TextInfo{ 0.655f, 0.612f, 0.961f, 1.0f };

		// A control's background: buttons, sliders (fields have their own, below)
		glm::vec4 Frame{ 0.227f, 0.255f, 0.294f, 1.0f };
		glm::vec4 FrameHovered{ 0.267f, 0.298f, 0.341f, 1.0f };
		glm::vec4 FramePressed{ 0.188f, 0.212f, 0.247f, 1.0f };
		glm::vec4 FrameBorder{ 1.0f, 1.0f, 1.0f, 0.07f };
		float FrameRounding = 6.0f;
		float FrameBorderWidth = 1.0f;

		// Buttons that start or end something (Button() with EButtonTone): Play is Go, Stop is Stop
		glm::vec4 ButtonGo{ 0.18f, 0.44f, 0.24f, 1.0f };
		glm::vec4 ButtonGoHovered{ 0.22f, 0.52f, 0.29f, 1.0f };
		glm::vec4 ButtonGoPressed{ 0.14f, 0.35f, 0.19f, 1.0f };
		glm::vec4 ButtonStop{ 0.56f, 0.19f, 0.19f, 1.0f };
		glm::vec4 ButtonStopHovered{ 0.66f, 0.24f, 0.23f, 1.0f };
		glm::vec4 ButtonStopPressed{ 0.45f, 0.15f, 0.15f, 1.0f };

		// The accent: a toggle that is on, a slider's filled part, section headers
		glm::vec4 Accent{ 0.561f, 0.518f, 0.941f, 1.0f };
		glm::vec4 AccentHovered{ 0.663f, 0.624f, 0.973f, 1.0f };

		// A toggle's or slider's track when off or empty, and their knobs
		glm::vec4 Track{ 0.208f, 0.235f, 0.271f, 1.0f };
		glm::vec4 TrackHovered{ 0.247f, 0.275f, 0.314f, 1.0f };
		glm::vec4 Knob{ 0.96f, 0.96f, 0.98f, 1.0f };

		// Thin lines, e.g. after a section header's label
		glm::vec4 Separator{ 1.0f, 1.0f, 1.0f, 0.10f };

		// The drop shadow under buttons and knobs; it shrinks to PressedShadow of itself while pressed
		PainterShadow ControlShadow{ { 0.0f, 0.0f, 0.0f, 0.35f }, { 0.0f, 1.5f }, 3.0f };
		float PressedShadow = 0.4f;
		// How far a button's label moves down while pressed, in pixels
		float PressedTextOffset = 1.0f;

		// The glow around a control with keyboard focus
		PainterGlow FocusGlow{ { 0.561f, 0.518f, 0.941f, 0.8f }, 6.0f, 0.5f };

		// Toggle: the track's width and height, and the knob's radius, as fractions of the frame
		// height. The knob shrinks to KnobPressedScale of itself while pressed.
		float ToggleWidth = 1.7f;
		float ToggleHeight = 0.8f;
		float ToggleKnobRadius = 0.32f;
		float KnobPressedScale = 0.88f;

		// Slider: the knob's radius and the track's height as fractions of the frame height; the
		// knob grows by KnobHoverGrowth of itself when hovered. The value's text sits right of
		// the track, ValueGap pixels away.
		float SliderKnobRadius = 0.3f;
		float SliderTrackHeight = 0.14f;
		float KnobHoverGrowth = 0.12f;
		float ValueGap = 8.0f;

		// Section header: an accent bar left of the label, and a line after it
		float SectionBarWidth = 3.0f;
		float SectionBarGap = 8.0f;
		PainterGlow SectionBarGlow{ { 0.561f, 0.518f, 0.941f, 0.6f }, 6.0f, 0.4f };
		float SectionLineWidth = 1.0f;

		// The window chrome (UIChrome.h): the backdrop behind the dock space, and the card every
		// window is painted as
		glm::vec4 Backdrop{ 0.071f, 0.082f, 0.098f, 1.0f };
		// Docked cards are slightly translucent, so a window's glow (ambilight) shows faintly
		// through its neighbours. Floating cards stay opaque.
		glm::vec4 Card{ 0.141f, 0.165f, 0.184f, 0.851f };
		// A hairline just inside each card's edge, so cards stand apart from the backdrop
		glm::vec4 CardBorder{ 1.0f, 1.0f, 1.0f, 0.05f };
		float CardRounding = 8.0f;
		// The gap between docked cards, which is also where the splitter between them is
		float CardGap = 6.0f;
		PainterShadow CardShadow{ { 0.0f, 0.0f, 0.0f, 0.45f }, { 0.0f, 2.0f }, 10.0f };
		// Floating windows, popups, menus and tooltips lie higher: lighter, with a stronger shadow
		glm::vec4 FloatingCard{ 0.157f, 0.18f, 0.204f, 1.0f };
		PainterShadow FloatingCardShadow{ { 0.0f, 0.0f, 0.0f, 0.6f }, { 0.0f, 8.0f }, 24.0f };
		// Glass (UIChrome.h): floating windows, popups, menus and tooltips in the main window show
		// what lies behind them, blurred, with this tint laid over it at its alpha, bent and lit
		// near their edge
		glm::vec4 GlassTint{ 0.118f, 0.137f, 0.161f, 0.651f };
		PainterGlass Glass{ 6.0f, 12.0f, 0.15f };
		// A splitter between docked cards shows only while hovered or dragged: a line this wide,
		// as long as the cards beside it less their rounding
		glm::vec4 Splitter{ 0.561f, 0.518f, 0.941f, 0.902f };
		float SplitterWidth = 2.0f;

		// Tab bars, docked and in windows (UIChrome.h): ImGui draws only the labels and close
		// buttons. Behind them, a pill behind the selected tab and behind a hovered one, TabPillInset
		// pixels from the bar's top and bottom; an underline under the selected tab, which slides
		// to another tab when it is selected; and a line (Separator) under the whole bar.
		glm::vec4 TabSelected{ 1.0f, 1.0f, 1.0f, 0.08f };
		glm::vec4 TabHovered{ 1.0f, 1.0f, 1.0f, 0.05f };
		float TabPillInset = 3.0f;
		glm::vec4 TabUnderline{ 0.718f, 0.659f, 0.984f, 1.0f };
		// The underline of a tab bar whose window doesn't have focus
		glm::vec4 TabUnderlineDimmed{ 0.616f, 0.639f, 0.675f, 0.702f };
		float TabUnderlineWidth = 2.0f;

		// Tree rows (TreeRow()): the pill behind the selected row and a hovered one, RowPillMargin
		// in from the row's ends and RowPillInset from its top and bottom; the guides down each
		// indentation level; and the icons
		glm::vec4 RowSelected{ 0.624f, 0.616f, 0.984f, 1.0f };
		glm::vec4 RowHovered{ 1.0f, 1.0f, 1.0f, 0.06f };
		float RowPillMargin = 4.0f;
		float RowPillInset = 1.0f;
		glm::vec4 IndentGuide{ 1.0f, 1.0f, 1.0f, 0.08f };
		glm::vec4 FolderIcon{ 0.788f, 0.804f, 0.839f, 1.0f };
		glm::vec4 EntityIcon{ 0.788f, 0.804f, 0.839f, 1.0f };

		// Fields: text, number and search fields (InputText(), DragScalar(), SearchField()). ImGui
		// edits and draws their text; the frame behind it is painted, darker than the card, so it
		// reads as a place to type. While a field is edited, its border turns to the accent and it
		// glows faintly. ImGui draws the text selection and the cursor in these colours.
		glm::vec4 Field{ 0.098f, 0.114f, 0.129f, 1.0f };
		glm::vec4 FieldHovered{ 0.122f, 0.141f, 0.161f, 1.0f };
		glm::vec4 FieldBorder{ 1.0f, 1.0f, 1.0f, 0.078f };
		PainterGlow FieldEditGlow{ { 0.561f, 0.518f, 0.941f, 0.4f }, 5.0f, 0.4f };
		glm::vec4 TextSelection{ 0.561f, 0.518f, 0.941f, 0.302f };
		glm::vec4 TextCursor{ 0.718f, 0.659f, 0.984f, 1.0f };
		// A search field's magnifier, as a fraction of the frame height
		float SearchIconSize = 0.55f;

		// The letters before each component of a vector field (DragVector()): X, Y, Z and W
		glm::vec4 AxisX{ 0.94f, 0.38f, 0.38f, 1.0f };
		glm::vec4 AxisY{ 0.50f, 0.80f, 0.36f, 1.0f };
		glm::vec4 AxisZ{ 0.38f, 0.60f, 0.98f, 1.0f };
		glm::vec4 AxisW{ 0.616f, 0.639f, 0.675f, 1.0f };

		// Property rows (BeginPropertyRow()): the label column takes PropertyLabelWidth of the row,
		// the value the rest. A hovered row shows the tree rows' hover pill.
		glm::vec4 PropertyLabel{ 0.753f, 0.773f, 0.804f, 1.0f };
		float PropertyLabelWidth = 0.4f;

		// Collapsing headers (CollapsingHeader()), e.g. a component in the details panel: a pill
		// across the window
		glm::vec4 Header{ 1.0f, 1.0f, 1.0f, 0.045f };
		glm::vec4 HeaderHovered{ 1.0f, 1.0f, 1.0f, 0.08f };

		// Checkboxes (Checkbox()): a rounded box CheckboxSize of the frame height, a field when
		// off, filled with the accent and ticked when on. The tick is CheckmarkWidth of the box
		// thick, in the knobs' colour; menu items tick in the accent.
		float CheckboxSize = 0.72f;
		float CheckboxRounding = 4.0f;
		float CheckmarkWidth = 0.13f;

		// Menu items and selectables (MenuItem(), Selectable()): the pill behind a hovered one, and
		// the selection's (RowSelected). Shortcuts are muted.
		glm::vec4 MenuItemHovered{ 1.0f, 1.0f, 1.0f, 0.09f };
		// Menu bar buttons (MenuBarButton()), e.g. File in the titlebar, are flat: the menu items'
		// hover pill, and the selection's (RowSelected) while their menu is open.

		// The window's frame (WindowsWindow): the titlebar, a toolbar under it and a status bar at
		// the bottom, each the window's width, filled in its colour. A hairline (BarBorder) runs
		// where each meets the dock space's side. The toolbar and status bar are there only when
		// the application draws into them (IWindow::SetFrameContent()).
		float TitlebarHeight = 32.0f;
		float ToolbarHeight = 38.0f;
		float StatusBarHeight = 24.0f;
		glm::vec4 Titlebar{ 0.094f, 0.114f, 0.133f, 1.0f };
		glm::vec4 Toolbar{ 0.133f, 0.157f, 0.173f, 1.0f };
		glm::vec4 StatusBar{ 0.098f, 0.122f, 0.141f, 1.0f };
		glm::vec4 BarBorder{ 1.0f, 1.0f, 1.0f, 0.04f };
		// An upright line between groups in a bar (BarDivider()), BarDividerHeight of the bar's
		// content height
		glm::vec4 BarDivider{ 1.0f, 1.0f, 1.0f, 0.28f };
		float BarDividerHeight = 0.5f;
		// Tabs in a bar (BarTab(), DocumentTab()): a raised tab, rounded at the top, behind the
		// selected one and every document; the hover pill (MenuItemHovered) behind the others.
		// A selected bar tab is underlined (TabUnderline).
		glm::vec4 BarTab{ 0.173f, 0.196f, 0.22f, 1.0f };
		glm::vec4 BarTabBorder{ 1.0f, 1.0f, 1.0f, 0.07f };
		float BarTabRounding = 6.0f;
		// The logo's place at the titlebar's left, a circle until there is a logo (TitlebarLogo())
		glm::vec4 Logo{ 0.882f, 0.894f, 0.925f, 1.0f };
		// Icons on buttons (IconButton()), IconSize of the button's height
		float IconSize = 0.45f;
		// A button that does the main thing of its bar, e.g. Build (EButtonTone::Primary): a
		// dark fill with an accent outline
		glm::vec4 ButtonPrimary{ 0.157f, 0.176f, 0.212f, 1.0f };
		glm::vec4 ButtonPrimaryHovered{ 0.184f, 0.208f, 0.247f, 1.0f };
		glm::vec4 ButtonPrimaryPressed{ 0.137f, 0.153f, 0.184f, 1.0f };
		glm::vec4 ButtonPrimaryBorder{ 0.561f, 0.518f, 0.941f, 0.451f };
		// The status bar's icons (StatusItem()): all is well, a warning, an error
		glm::vec4 StatusOk{ 0.42f, 0.82f, 0.36f, 1.0f };
		glm::vec4 StatusWarning{ 0.95f, 0.72f, 0.25f, 1.0f };
		glm::vec4 StatusError{ 0.92f, 0.33f, 0.3f, 1.0f };
		// The mark inside a status icon, on its colour
		glm::vec4 StatusIconMark{ 0.04f, 0.07f, 0.06f, 1.0f };

		// The titlebar's window controls (WindowControlButton()) are flat, filled while hovered
		// or pressed, close in red. Their icons fit a square WindowControlIconSize wide, in lines
		// WindowControlIconWidth thick.
		glm::vec4 WindowControlHovered{ 1.0f, 1.0f, 1.0f, 0.08f };
		glm::vec4 WindowControlPressed{ 1.0f, 1.0f, 1.0f, 0.14f };
		glm::vec4 WindowCloseHovered{ 0.77f, 0.17f, 0.11f, 1.0f };
		glm::vec4 WindowClosePressed{ 0.63f, 0.14f, 0.09f, 1.0f };
		float WindowControlIconSize = 10.0f;
		float WindowControlIconWidth = 1.0f;
		// The border around the editor's window while it has focus, WindowFocusBorderWidth wide and
		// rounded as Windows 11 rounds the window (none while maximised)
		glm::vec4 WindowFocusBorder{ 0.431f, 0.392f, 0.769f, 1.0f };
		float WindowFocusBorderWidth = 3.0f;
		float WindowRounding = 10.0f;

		// Colour swatches (ColorSwatch()): a hairline around the colour, lighter when hovered
		glm::vec4 SwatchBorder{ 1.0f, 1.0f, 1.0f, 0.15f };
		glm::vec4 SwatchBorderHovered{ 1.0f, 1.0f, 1.0f, 0.45f };

		// Tables (BeginTable()): a strip behind the header row, with separators (Separator) between
		// its cells; stripes on every other row; and an accent line (Splitter) on a column border
		// being hovered or dragged
		glm::vec4 TableHeader{ 1.0f, 1.0f, 1.0f, 0.05f };
		glm::vec4 TableRowStripe{ 1.0f, 1.0f, 1.0f, 0.025f };

		// Asset cards (AssetCard()): AssetCardWidth wide, a square picture over the name,
		// AssetCardPadding inside; lit while hovered, and in the selection's colour (RowSelected)
		// with an accent outline while selected. Files without a thumbnail show a page in FileIcon.
		float AssetCardWidth = 92.0f;
		float AssetCardPadding = 6.0f;
		glm::vec4 AssetCard{ 1.0f, 1.0f, 1.0f, 0.03f };
		glm::vec4 AssetCardHovered{ 1.0f, 1.0f, 1.0f, 0.07f };
		glm::vec4 FileIcon{ 0.62f, 0.67f, 0.78f, 1.0f };

		// Scrollbars (UIChrome.h): a thin pill in the middle of ImGui's scrollbar, wider while the
		// scrollbar is hovered, in the accent while it is dragged
		glm::vec4 ScrollbarThumb{ 1.0f, 1.0f, 1.0f, 0.16f };
		glm::vec4 ScrollbarThumbHovered{ 1.0f, 1.0f, 1.0f, 0.32f };
		glm::vec4 ScrollbarThumbHeld{ 0.561f, 0.518f, 0.941f, 0.851f };
		float ScrollbarThumbWidth = 4.0f;
		float ScrollbarThumbHoveredWidth = 8.0f;

		// What disabled widgets keep of their colours' alpha
		float DisabledAlpha = 0.45f;
		// How fast hover, press and knobs ease, per second: after 1 / AnimationSpeed seconds, about
		// two thirds of the way are done
		float AnimationSpeed = 18.0f;
	};

	// The theme the painted widgets use now
	Theme& GetTheme();
}

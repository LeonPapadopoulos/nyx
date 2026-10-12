#pragma once

#include "Painter.h"

#include <glm/glm.hpp>

namespace Nyx::UI
{
	// How the painted widgets (UIWidgets.h) and the window chrome (UIChrome.h) look. Every
	// colour, radius, size and effect they use comes from here, never from a literal in a widget,
	// so that the whole look can change in one place (Design/editor-ui-architecture.md, 3.10.3). Overhaul change 9 fills it from a theme
	// file and a theme editor; for now it holds the editor's dark look.
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
		glm::vec4 Text{ 0.90f, 0.91f, 0.94f, 1.0f };
		glm::vec4 TextMuted{ 0.55f, 0.58f, 0.66f, 1.0f };

		// A control's background: buttons, sliders
		glm::vec4 Frame{ 0.22f, 0.23f, 0.27f, 1.0f };
		glm::vec4 FrameHovered{ 0.28f, 0.29f, 0.34f, 1.0f };
		glm::vec4 FramePressed{ 0.17f, 0.18f, 0.21f, 1.0f };
		glm::vec4 FrameBorder{ 1.0f, 1.0f, 1.0f, 0.07f };
		float FrameRounding = 6.0f;
		float FrameBorderWidth = 1.0f;

		// The accent: a toggle that is on, a slider's filled part, section headers
		glm::vec4 Accent{ 1.0f, 0.55f, 0.15f, 1.0f };
		glm::vec4 AccentHovered{ 1.0f, 0.64f, 0.30f, 1.0f };

		// A toggle's or slider's track when off or empty, and their knobs
		glm::vec4 Track{ 0.32f, 0.33f, 0.38f, 1.0f };
		glm::vec4 TrackHovered{ 0.38f, 0.39f, 0.45f, 1.0f };
		glm::vec4 Knob{ 0.96f, 0.96f, 0.98f, 1.0f };

		// Thin lines, e.g. after a section header's label
		glm::vec4 Separator{ 1.0f, 1.0f, 1.0f, 0.10f };

		// The drop shadow under buttons and knobs; it shrinks to PressedShadow of itself while pressed
		Engine::PainterShadow ControlShadow{ { 0.0f, 0.0f, 0.0f, 0.35f }, { 0.0f, 1.5f }, 3.0f };
		float PressedShadow = 0.4f;
		// How far a button's label moves down while pressed, in pixels
		float PressedTextOffset = 1.0f;

		// The glow around a control with keyboard focus
		Engine::PainterGlow FocusGlow{ { 1.0f, 0.55f, 0.15f, 0.9f }, 6.0f, 0.6f };

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
		Engine::PainterGlow SectionBarGlow{ { 1.0f, 0.55f, 0.15f, 0.8f }, 6.0f, 0.5f };
		float SectionLineWidth = 1.0f;

		// The window chrome (UIChrome.h): the backdrop behind the dock space, and the card every
		// window is painted as
		glm::vec4 Backdrop{ 0.045f, 0.047f, 0.055f, 1.0f };
		// Docked cards are slightly translucent, so a window's glow (ambilight) shows faintly
		// through its neighbours. Floating cards stay opaque.
		glm::vec4 Card{ 0.098f, 0.1f, 0.112f, 0.85f };
		// A hairline just inside each card's edge, so cards stand apart from the backdrop
		glm::vec4 CardBorder{ 1.0f, 1.0f, 1.0f, 0.05f };
		float CardRounding = 8.0f;
		// The gap between docked cards, which is also where the splitter between them is
		float CardGap = 6.0f;
		Engine::PainterShadow CardShadow{ { 0.0f, 0.0f, 0.0f, 0.45f }, { 0.0f, 2.0f }, 10.0f };
		// Floating windows, popups, menus and tooltips lie higher: lighter, with a stronger shadow
		glm::vec4 FloatingCard{ 0.135f, 0.138f, 0.155f, 1.0f };
		Engine::PainterShadow FloatingCardShadow{ { 0.0f, 0.0f, 0.0f, 0.6f }, { 0.0f, 8.0f }, 24.0f };
		// A splitter between docked cards shows only while hovered or dragged: a line this wide,
		// as long as the cards beside it less their rounding
		glm::vec4 Splitter{ 1.0f, 0.55f, 0.15f, 0.9f };
		float SplitterWidth = 2.0f;

		// Tab bars, docked and in windows (UIChrome.h): ImGui draws only the labels and close
		// buttons. Behind them, a pill behind the selected tab and behind a hovered one, TabPillInset
		// pixels from the bar's top and bottom; an underline under the selected tab, which slides
		// to another tab when it is selected; and a line (Separator) under the whole bar.
		glm::vec4 TabSelected{ 1.0f, 1.0f, 1.0f, 0.08f };
		glm::vec4 TabHovered{ 1.0f, 1.0f, 1.0f, 0.05f };
		float TabPillInset = 3.0f;
		glm::vec4 TabUnderline{ 1.0f, 0.55f, 0.15f, 1.0f };
		// The underline of a tab bar whose window doesn't have focus
		glm::vec4 TabUnderlineDimmed{ 0.55f, 0.58f, 0.66f, 0.7f };
		float TabUnderlineWidth = 2.0f;

		// What disabled widgets keep of their colours' alpha
		float DisabledAlpha = 0.45f;
		// How fast hover, press and knobs ease, per second: after 1 / AnimationSpeed seconds, about
		// two thirds of the way are done
		float AnimationSpeed = 18.0f;
	};

	// The theme the painted widgets use now
	Theme& GetTheme();
}

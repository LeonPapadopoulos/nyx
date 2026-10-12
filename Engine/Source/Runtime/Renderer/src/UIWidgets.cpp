#include "UIWidgets.h"

#include "UIAnimation.h"
#include "UITheme.h"
#include "WindowPainter.h"

// ImGui's behaviour functions and item state: only the widget files use imgui_internal.h, whose
// API changes between ImGui versions more than the public one.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Nyx::UI
{
	using Engine::EPainterLineCap;
	using Engine::PainterEffects;
	using Engine::PainterRect;
	using Engine::PainterStyle;

	namespace
	{
		PainterRect ToPainterRect(const ImRect& rect)
		{
			return PainterRect{ { rect.Min.x, rect.Min.y }, { rect.Max.x, rect.Max.y } };
		}

		// The rect `size` big at ImGui's cursor
		ImRect RectAtCursor(const ImGuiWindow& window, glm::vec2 size)
		{
			const ImVec2 min = window.DC.CursorPos;
			return ImRect(min, ImVec2(min.x + size.x, min.y + size.y));
		}

		// What a label shows: the text before "##"
		std::string_view VisibleLabel(const char* label)
		{
			return std::string_view(label, static_cast<size_t>(ImGui::FindRenderedTextEnd(label) - label));
		}

		glm::vec2 CalcTextSize(std::string_view text)
		{
			const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
			return glm::vec2(size.x, size.y);
		}

		// A colour as the widget shows it: disabled widgets are faded.
		glm::vec4 Shade(glm::vec4 color, const WidgetLook& look, const Theme& theme)
		{
			if (look.bDisabled)
			{
				color.a *= theme.DisabledAlpha;
			}
			return color;
		}

		// A control's effects: its shadow (smaller while pressed), and the glow of keyboard focus
		PainterEffects ControlEffects(const WidgetLook& look, const Theme& theme, bool bShadow)
		{
			PainterEffects effects;
			if (bShadow)
			{
				effects.Shadow = theme.ControlShadow;
				effects.Shadow.Color = Shade(effects.Shadow.Color, look, theme);
				effects.Shadow.Color.a *= glm::mix(1.0f, theme.PressedShadow, look.Pressed);
			}
			effects.Glow = theme.FocusGlow;
			effects.Glow.Strength *= look.Focused;
			return effects;
		}

		PainterStyle Filled(const glm::vec4& color, const PainterEffects& effects = PainterEffects{})
		{
			PainterStyle style = PainterStyle::Filled(color);
			style.Effects = effects;
			return style;
		}

		void PaintText(WindowPainter& painter, glm::vec2 position, const glm::vec4& color, std::string_view text)
		{
			painter.AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, color, text);
		}

		// The look from this frame's interaction with the item ImGui added last, eased over frames
		WidgetLook AnimateLook(ImGuiID id, bool bHovered, bool bPressed, const Theme& theme)
		{
			const ImGuiContext& g = *GImGui;
			const bool bFocused = g.NavId == id && g.NavCursorVisible;

			WidgetLook look;
			look.bDisabled = (g.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
			look.Hovered = Animate(ImHashStr("Hovered", 0, id), bHovered ? 1.0f : 0.0f, theme.AnimationSpeed);
			look.Pressed = Animate(ImHashStr("Pressed", 0, id), bPressed ? 1.0f : 0.0f, theme.AnimationSpeed);
			look.Focused = Animate(ImHashStr("Focused", 0, id), bFocused ? 1.0f : 0.0f, theme.AnimationSpeed);
			return look;
		}

		// The part of a slider's frame the knob moves along: the frame without the value's text
		// at its right. ImGui's SliderBehavior() maps the mouse to the value over this rect.
		ImRect GetSliderTrack(const PainterRect& frame, float valueWidth, const Theme& theme)
		{
			const float right = frame.Max.x - ImGui::GetStyle().FramePadding.x - valueWidth - theme.ValueGap;
			return ImRect(ImVec2(frame.Min.x, frame.Min.y), ImVec2(std::max(right, frame.Min.x), frame.Max.y));
		}

		// Where the knob's centre is at `fraction` along the track, as ImGui's SliderBehavior()
		// lays out its grab: 2 pixels in from each end, and a grab of style.GrabMinSize.
		float GetSliderKnobX(const ImRect& track, float fraction)
		{
			constexpr float GrabPadding = 2.0f;
			const float sliderSize = std::max(track.GetWidth() - GrabPadding * 2.0f, 0.0f);
			const float grabSize = std::min(ImGui::GetStyle().GrabMinSize, sliderSize);
			return track.Min.x + GrabPadding + grabSize * 0.5f + fraction * (sliderSize - grabSize);
		}
	}

	bool Button(const char* label, const glm::vec2& sizeArgument)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		// As ImGui::ButtonEx()
		const ImGuiID id = window->GetID(label);
		const glm::vec2 fitted = CalcButtonSize(label);
		const ImVec2 size = ImGui::CalcItemSize(ImVec2(sizeArgument.x, sizeArgument.y), fitted.x, fitted.y);
		const ImRect rect = RectAtCursor(*window, glm::vec2(size.x, size.y));
		ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
		if (!ImGui::ItemAdd(rect, id))
		{
			return false;
		}

		bool bHovered = false;
		bool bHeld = false;
		const bool bClicked = ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld);

		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, GetTheme());
		PaintButton(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), look);
		return bClicked;
	}

	bool Toggle(const char* label, bool* value)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		// As ImGui::Checkbox(): the switch and its label are one item.
		const ImGuiID id = window->GetID(label);
		const glm::vec2 size = CalcToggleSize(label);
		const ImRect rect = RectAtCursor(*window, size);
		ImGui::ItemSize(ImVec2(size.x, size.y), ImGui::GetStyle().FramePadding.y);
		if (!ImGui::ItemAdd(rect, id))
		{
			return false;
		}

		bool bHovered = false;
		bool bHeld = false;
		const bool bClicked = ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld);
		if (bClicked)
		{
			*value = !*value;
			ImGui::MarkItemEdited(id);
		}

		const Theme& theme = GetTheme();
		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		const float on = Animate(ImHashStr("On", 0, id), *value ? 1.0f : 0.0f, theme.AnimationSpeed);
		PaintToggle(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), on, look);
		return bClicked;
	}

	bool SliderFloat(const char* label, float* value, float min, float max, const char* format)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const Theme& theme = GetTheme();

		// As ImGui::SliderScalar(), without Ctrl+click to type a value
		const ImGuiID id = window->GetID(label);
		const std::string_view shownLabel = VisibleLabel(label);
		const float labelWidth = CalcTextSize(shownLabel).x;
		const ImRect frame = RectAtCursor(*window, CalcSliderFrameSize());
		const ImRect total(frame.Min, ImVec2(frame.Max.x + (labelWidth > 0.0f ? style.ItemInnerSpacing.x + labelWidth : 0.0f), frame.Max.y));
		ImGui::ItemSize(total, style.FramePadding.y);
		if (!ImGui::ItemAdd(total, id, &frame))
		{
			return false;
		}

		const bool bHovered = ImGui::ItemHoverable(frame, id, g.LastItemData.ItemFlags);
		const bool bClicked = bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None, id);
		if (bClicked || g.NavActivateId == id)
		{
			if (bClicked)
			{
				ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
			}
			ImGui::SetActiveID(id, window);
			ImGui::SetFocusID(id, window);
			ImGui::FocusWindow(window);
			g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
		}

		const float valueWidth = CalcSliderValueWidth(min, max, format);
		const ImRect track = GetSliderTrack(ToPainterRect(frame), valueWidth, theme);
		ImRect grab;
		const bool bChanged = ImGui::SliderBehavior(track, id, ImGuiDataType_Float, value, &min, &max, format, ImGuiSliderFlags_None, &grab);
		if (bChanged)
		{
			ImGui::MarkItemEdited(id);
		}

		char valueText[64];
		ImFormatString(valueText, sizeof(valueText), format, *value);
		const float fraction = max > min ? std::clamp((*value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
		const WidgetLook look = AnimateLook(id, bHovered, g.ActiveId == id, theme);
		PaintSlider(GetWindowPainter(), ToPainterRect(frame), shownLabel, fraction, valueText, valueWidth, look);
		return bChanged;
	}

	void SectionHeader(const char* label)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}

		// Not interactive, and skipped by keyboard navigation; an item only for layout and the
		// last-item queries
		const ImGuiID id = window->GetID(label);
		const glm::vec2 size = CalcSectionHeaderSize();
		const ImRect rect = RectAtCursor(*window, size);
		ImGui::ItemSize(ImVec2(size.x, size.y), ImGui::GetStyle().FramePadding.y);
		if (!ImGui::ItemAdd(rect, id, nullptr, ImGuiItemFlags_NoNav))
		{
			return;
		}
		PaintSectionHeader(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label));
	}

	void PaintButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();

		const glm::vec4 fill = glm::mix(glm::mix(theme.Frame, theme.FrameHovered, look.Hovered), theme.FramePressed, look.Pressed);
		painter.AddRoundedRect(rect, theme.FrameRounding, Filled(Shade(fill, look, theme), ControlEffects(look, theme, true)));
		painter.AddRoundedRect(rect, theme.FrameRounding, PainterStyle::Outlined(Shade(theme.FrameBorder, look, theme), theme.FrameBorderWidth));

		// The label centred, pushed down a little while pressed
		const glm::vec2 textSize = CalcTextSize(label);
		const glm::vec2 centre = (rect.Min + rect.Max) * 0.5f;
		const glm::vec2 textPosition = centre - textSize * 0.5f + glm::vec2(0.0f, theme.PressedTextOffset * look.Pressed);
		PaintText(painter, textPosition, Shade(theme.Text, look, theme), label);
	}

	void PaintToggle(WindowPainter& painter, const PainterRect& rect, std::string_view label, float on, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const float frameHeight = rect.GetHeight();

		// The track, centred on the frame's height
		const float trackHeight = frameHeight * theme.ToggleHeight;
		const float trackTop = rect.Min.y + (frameHeight - trackHeight) * 0.5f;
		const PainterRect track{ { rect.Min.x, trackTop }, { rect.Min.x + frameHeight * theme.ToggleWidth, trackTop + trackHeight } };
		const glm::vec4 off = glm::mix(theme.Track, theme.TrackHovered, look.Hovered);
		const glm::vec4 onColor = glm::mix(theme.Accent, theme.AccentHovered, look.Hovered);
		painter.AddRoundedRect(track, trackHeight * 0.5f, Filled(Shade(glm::mix(off, onColor, on), look, theme), ControlEffects(look, theme, false)));

		// The knob, from the left end (off) to the right end (on)
		const float inset = trackHeight * 0.5f;
		const glm::vec2 knob{ glm::mix(track.Min.x + inset, track.Max.x - inset, on), trackTop + inset };
		const float knobRadius = frameHeight * theme.ToggleKnobRadius * glm::mix(1.0f, theme.KnobPressedScale, look.Pressed);
		PainterEffects knobShadow;
		knobShadow.Shadow = theme.ControlShadow;
		knobShadow.Shadow.Color = Shade(knobShadow.Shadow.Color, look, theme);
		painter.AddCircle(knob, knobRadius, Filled(Shade(theme.Knob, look, theme), knobShadow));

		if (!label.empty())
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			PaintText(painter, { track.Max.x + style.ItemInnerSpacing.x, rect.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), label);
		}
	}

	void PaintSlider(WindowPainter& painter, const PainterRect& frame, std::string_view label, float fraction, std::string_view valueText, float valueWidth, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float frameHeight = frame.GetHeight();
		const float centreY = (frame.Min.y + frame.Max.y) * 0.5f;

		const glm::vec4 fill = glm::mix(theme.Frame, theme.FrameHovered, look.Hovered);
		painter.AddRoundedRect(frame, theme.FrameRounding, Filled(Shade(fill, look, theme), ControlEffects(look, theme, false)));
		painter.AddRoundedRect(frame, theme.FrameRounding, PainterStyle::Outlined(Shade(theme.FrameBorder, look, theme), theme.FrameBorderWidth));

		// The track from the knob's lowest to its highest place, filled with the accent up to the knob
		const ImRect track = GetSliderTrack(frame, valueWidth, theme);
		const float startX = GetSliderKnobX(track, 0.0f);
		const float endX = GetSliderKnobX(track, 1.0f);
		const float knobX = GetSliderKnobX(track, fraction);
		const float trackHalfHeight = frameHeight * theme.SliderTrackHeight * 0.5f;
		const glm::vec4 empty = glm::mix(theme.Track, theme.TrackHovered, look.Hovered);
		const glm::vec4 filled = glm::mix(theme.Accent, theme.AccentHovered, look.Hovered);
		painter.AddRoundedRect(PainterRect{ { startX, centreY - trackHalfHeight }, { endX, centreY + trackHalfHeight } }, trackHalfHeight, Filled(Shade(empty, look, theme)));
		painter.AddRoundedRect(PainterRect{ { startX, centreY - trackHalfHeight }, { knobX, centreY + trackHalfHeight } }, trackHalfHeight, Filled(Shade(filled, look, theme)));

		// The knob: a little larger when hovered, smaller while held
		const float knobRadius = frameHeight * theme.SliderKnobRadius * (1.0f + theme.KnobHoverGrowth * look.Hovered) * glm::mix(1.0f, theme.KnobPressedScale, look.Pressed);
		PainterEffects knobShadow;
		knobShadow.Shadow = theme.ControlShadow;
		knobShadow.Shadow.Color = Shade(knobShadow.Shadow.Color, look, theme);
		painter.AddCircle({ knobX, centreY }, knobRadius, Filled(Shade(theme.Knob, look, theme), knobShadow));

		// The value right-aligned in its room, then the label right of the frame
		const float textY = frame.Min.y + style.FramePadding.y;
		const float valueRight = frame.Max.x - style.FramePadding.x;
		PaintText(painter, { valueRight - CalcTextSize(valueText).x, textY }, Shade(theme.Text, look, theme), valueText);
		if (!label.empty())
		{
			PaintText(painter, { frame.Max.x + style.ItemInnerSpacing.x, textY }, Shade(theme.Text, look, theme), label);
		}
	}

	void PaintSectionHeader(WindowPainter& painter, const PainterRect& rect, std::string_view label)
	{
		const Theme& theme = GetTheme();
		const float fontSize = ImGui::GetFontSize();
		const float textTop = rect.Min.y + (rect.GetHeight() - fontSize) * 0.5f;

		// The accent bar, as high as the text, with a soft glow
		const PainterRect bar{ { rect.Min.x, textTop }, { rect.Min.x + theme.SectionBarWidth, textTop + fontSize } };
		PainterEffects glow;
		glow.Glow = theme.SectionBarGlow;
		painter.AddRoundedRect(bar, theme.SectionBarWidth * 0.5f, Filled(theme.Accent, glow));

		const float textX = bar.Max.x + theme.SectionBarGap;
		PaintText(painter, { textX, textTop }, theme.Text, label);

		// A line from after the label to the rect's right end
		const float lineStart = textX + CalcTextSize(label).x + theme.SectionBarGap;
		const float centreY = textTop + fontSize * 0.5f;
		if (lineStart < rect.Max.x)
		{
			painter.AddLine({ lineStart, centreY }, { rect.Max.x, centreY }, theme.SectionLineWidth, EPainterLineCap::Flat, PainterStyle::Filled(theme.Separator));
		}
	}

	glm::vec2 CalcButtonSize(const char* label)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const glm::vec2 labelSize = CalcTextSize(VisibleLabel(label));
		return glm::vec2(labelSize.x + style.FramePadding.x * 2.0f, ImGui::GetFrameHeight());
	}

	glm::vec2 CalcToggleSize(const char* label)
	{
		const float labelWidth = CalcTextSize(VisibleLabel(label)).x;
		const float frameHeight = ImGui::GetFrameHeight();
		const float switchWidth = frameHeight * GetTheme().ToggleWidth;
		return glm::vec2(switchWidth + (labelWidth > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + labelWidth : 0.0f), frameHeight);
	}

	glm::vec2 CalcSliderFrameSize()
	{
		return glm::vec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
	}

	float CalcSliderValueWidth(float min, float max, const char* format)
	{
		char text[64];
		ImFormatString(text, sizeof(text), format, min);
		const float minWidth = ImGui::CalcTextSize(text).x;
		ImFormatString(text, sizeof(text), format, max);
		return std::max(minWidth, ImGui::CalcTextSize(text).x);
	}

	glm::vec2 CalcSectionHeaderSize()
	{
		return glm::vec2(std::max(ImGui::GetContentRegionAvail().x, 1.0f), ImGui::GetFrameHeight());
	}
}

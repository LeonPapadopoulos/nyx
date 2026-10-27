#include "UIWidgets.h"

#include "ImGuiConversions.h"
#include "UIAnimation.h"
#include "UITheme.h"
#include "WindowPainter.h"

// ImGui's behaviour functions and item state: only the widget files use imgui_internal.h, whose
// API changes between ImGui versions more than the public one.
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <vector>

namespace Nyx::UI
{
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

		// A tree row's arrow column and icon, as wide as the font is high
		float TreeRowArrowWidth()
		{
			return ImGui::GetFontSize();
		}

		float TreeRowIconSize()
		{
			return std::floor(ImGui::GetFontSize() * 0.9f);
		}

		// `color` with its alpha times `amount`
		glm::vec4 Fade(glm::vec4 color, float amount)
		{
			color.a *= amount;
			return color;
		}

		// A tree row's icon, drawn into `box`, a square
		void PaintTreeRowIcon(WindowPainter& painter, ETreeRowIcon icon, const PainterRect& box, const WidgetLook& look, const Theme& theme)
		{
			const float size = box.GetWidth();
			switch (icon)
			{
			case ETreeRowIcon::Folder:
			{
				// A tab at the top left, and the folder's body below it
				const glm::vec4 color = Shade(theme.FolderIcon, look, theme);
				painter.AddRoundedRect(PainterRect{ { box.Min.x, box.Min.y + size * 0.12f }, { box.Min.x + size * 0.45f, box.Min.y + size * 0.4f } }, size * 0.08f, PainterStyle::Filled(color));
				painter.AddRoundedRect(PainterRect{ { box.Min.x, box.Min.y + size * 0.25f }, { box.Max.x, box.Max.y - size * 0.1f } }, size * 0.12f, PainterStyle::Filled(color));
				break;
			}
			case ETreeRowIcon::Entity:
				// A rounded square, like a small box
				painter.AddRoundedRect(PainterRect{ { box.Min.x + size * 0.15f, box.Min.y + size * 0.15f }, { box.Max.x - size * 0.15f, box.Max.y - size * 0.15f } }, size * 0.15f,
					PainterStyle::Filled(Shade(theme.EntityIcon, look, theme)));
				break;
			case ETreeRowIcon::None:
				break;
			}
		}

		// A small triangle pointing right, turned down as `open` goes from 0 to 1, in a column
		// `width` wide centred on `centre`
		void PaintArrow(WindowPainter& painter, glm::vec2 centre, float width, float open, const glm::vec4& color)
		{
			const float size = width * 0.22f;
			const float angle = open * 1.5707963f;
			const auto turned = [&](glm::vec2 point)
			{
				return centre + glm::vec2(point.x * std::cos(angle) - point.y * std::sin(angle), point.x * std::sin(angle) + point.y * std::cos(angle));
			};
			painter.AddTriangle(turned({ size, 0.0f }), turned({ -size * 0.6f, -size }), turned({ -size * 0.6f, size }), PainterStyle::Filled(color));
		}

		// A guide down each of the `depth` tree levels a row is inside, under the arrows of the
		// rows above it. Each row's piece reaches half the item spacing past its top and bottom,
		// so the pieces of rows one under the other meet.
		void PaintIndentGuides(WindowPainter& painter, const PainterRect& rect, float contentX, int depth, const Theme& theme)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float arrowWidth = TreeRowArrowWidth();
			const float halfSpacing = style.ItemSpacing.y * 0.5f;
			for (int level = 0; level < depth; ++level)
			{
				const float x = std::floor(contentX - static_cast<float>(depth - level) * style.IndentSpacing + arrowWidth * 0.5f) + 0.5f;
				painter.AddLine({ x, rect.Min.y - halfSpacing }, { x, rect.Max.y + halfSpacing }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.IndentGuide));
			}
		}

		// The pill behind a row: RowPillMargin in from its ends, RowPillInset from its top and bottom
		PainterRect RowPill(const PainterRect& rect, const Theme& theme)
		{
			return PainterRect{ { rect.Min.x + theme.RowPillMargin, rect.Min.y + theme.RowPillInset }, { rect.Max.x - theme.RowPillMargin, rect.Max.y - theme.RowPillInset } };
		}

		// `text` cut to fit `width`, ending in "..." where it was cut. `storage` holds a cut text.
		std::string_view FitText(std::string_view text, float width, std::string& storage)
		{
			if (CalcTextSize(text).x <= width)
			{
				return text;
			}

			constexpr std::string_view Ellipsis = "...";
			const float room = width - CalcTextSize(Ellipsis).x;
			if (room <= 0.0f)
			{
				return {};
			}
			// Where the text passes `room`, at a whole character
			const char* end = text.data();
			ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), room, 0.0f, text.data(), text.data() + text.size(), &end);
			storage.assign(text.data(), end);
			storage += Ellipsis;
			return storage;
		}

		// A field's look before ImGui's widget for it runs: hovered as ImGui knew it the frame
		// before, and edited (typed in or dragged) as it is now
		struct FieldState
		{
			WidgetLook Look;
			float Editing = 0.0f;
		};

		FieldState AnimateFieldState(ImGuiID id, const Theme& theme)
		{
			const ImGuiContext& g = *GImGui;
			const bool bEditing = g.ActiveId == id;
			const bool bFocused = g.NavId == id && g.NavCursorVisible && !bEditing;

			FieldState state;
			state.Look.bDisabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
			state.Look.Hovered = Animate(ImHashStr("Hovered", 0, id), g.HoveredIdPreviousFrame == id ? 1.0f : 0.0f, theme.AnimationSpeed);
			state.Look.Focused = Animate(ImHashStr("Focused", 0, id), bFocused ? 1.0f : 0.0f, theme.AnimationSpeed);
			state.Editing = Animate(ImHashStr("Editing", 0, id), bEditing ? 1.0f : 0.0f, theme.AnimationSpeed);
			return state;
		}

		// While ImGui draws a field over its painted frame: no frame, border or keyboard cursor
		// of its own, and the theme's colours for the text selection and the text cursor
		class ScopedFieldStyle
		{
		public:
			explicit ScopedFieldStyle(const Theme& theme)
			{
				const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, clear);
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, clear);
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, clear);
				ImGui::PushStyleColor(ImGuiCol_NavCursor, clear);
				ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ToImVec4(theme.TextSelection));
				ImGui::PushStyleColor(ImGuiCol_InputTextCursor, ToImVec4(theme.TextCursor));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			}

			~ScopedFieldStyle()
			{
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(6);
			}

			ScopedFieldStyle(const ScopedFieldStyle&) = delete;
			ScopedFieldStyle& operator=(const ScopedFieldStyle&) = delete;
		};

		// A field's frame: darker than the card, lighter when hovered; while edited, an accent
		// border and a faint glow, and the focus glow while it has keyboard focus
		void PaintFieldFrame(WindowPainter& painter, const PainterRect& frame, float radius, float editing, const WidgetLook& look, const Theme& theme)
		{
			PainterEffects effects;
			if (editing >= look.Focused)
			{
				effects.Glow = theme.FieldEditGlow;
				effects.Glow.Strength *= editing;
			}
			else
			{
				effects.Glow = theme.FocusGlow;
				effects.Glow.Strength *= look.Focused;
			}
			const glm::vec4 fill = glm::mix(theme.Field, theme.FieldHovered, std::max(look.Hovered, editing));
			painter.AddRoundedRect(frame, radius, Filled(Shade(fill, look, theme), effects));
			const glm::vec4 border = glm::mix(theme.FieldBorder, theme.Accent, editing);
			painter.AddRoundedRect(frame, radius, PainterStyle::Outlined(Shade(border, look, theme), theme.FrameBorderWidth));
		}

		// A search field's magnifier: SearchIconLeft() in from the frame's left, as big as the
		// theme says
		float SearchIconLeft(float frameHeight)
		{
			return std::floor(frameHeight * 0.4f);
		}

		float SearchIconSize(float frameHeight, const Theme& theme)
		{
			return std::floor(frameHeight * theme.SearchIconSize);
		}

		// While ImGui draws a menu item or selectable that is painted after it: no highlight, text
		// or keyboard cursor of its own
		class ScopedInvisibleItem
		{
		public:
			ScopedInvisibleItem()
			{
				const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
				for (const ImGuiCol color : { ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_NavCursor, ImGuiCol_Text, ImGuiCol_TextDisabled })
				{
					ImGui::PushStyleColor(color, clear);
				}
			}

			~ScopedInvisibleItem()
			{
				ImGui::PopStyleColor(6);
			}

			ScopedInvisibleItem(const ScopedInvisibleItem&) = delete;
			ScopedInvisibleItem& operator=(const ScopedInvisibleItem&) = delete;
		};

		// The look of the item ImGui added last, from its state after it ran. In menus, the item
		// the keyboard is on shows as hovered, as ImGui's menus show it.
		WidgetLook AnimateLastItemLook(const Theme& theme, bool bKeyboardAsHover)
		{
			const ImGuiContext& g = *GImGui;
			const ImGuiID id = g.LastItemData.ID;
			const bool bKeyboard = g.NavId == id && g.NavCursorVisible;
			const bool bHovered = ImGui::IsItemHovered() || (bKeyboardAsHover && bKeyboard);
			WidgetLook look = AnimateLook(id, bHovered, bHovered && ImGui::IsItemActive(), theme);
			if (bKeyboardAsHover)
			{
				look.Focused = 0.0f;
			}
			return look;
		}

		// `point`, relative to `centre`, turned by `angle` (radians) around it
		glm::vec2 Turned(glm::vec2 centre, glm::vec2 point, float angle)
		{
			return centre + glm::vec2(point.x * std::cos(angle) - point.y * std::sin(angle), point.x * std::sin(angle) + point.y * std::cos(angle));
		}

		// While painting behind a table's cells: the table's background channel for the rows being
		// laid out, with the clip rect ImGui gives row backgrounds there, as
		// ImGui::TablePushBackgroundChannel() sets them; given back after.
		class ScopedTableBackground
		{
		public:
			explicit ScopedTableBackground(ImGuiTable& table)
				: Table(table)
				, Window(*table.InnerWindow)
				, ClipRectBefore(Window.ClipRect)
				, ChannelBefore(table.DrawSplitter->_Current)
			{
				ImGui::SetWindowClipRectBeforeSetChannel(&Window, table.Bg2ClipRectForDrawCmd);
				table.DrawSplitter->SetCurrentChannel(Window.DrawList, table.Bg2DrawChannelCurrent);
			}

			~ScopedTableBackground()
			{
				ImGui::SetWindowClipRectBeforeSetChannel(&Window, ClipRectBefore);
				Table.DrawSplitter->SetCurrentChannel(Window.DrawList, ChannelBefore);
			}

			ScopedTableBackground(const ScopedTableBackground&) = delete;
			ScopedTableBackground& operator=(const ScopedTableBackground&) = delete;

		private:
			ImGuiTable& Table;
			ImGuiWindow& Window;
			ImRect ClipRectBefore;
			int ChannelBefore = 0;
		};

		// ImGui's colours of a table's backgrounds and borders, transparent while a painted table
		// is built. ImGui reads its border colours in BeginTable(), the backgrounds' as rows end,
		// and draws a hovered or dragged column border in the separator's in EndTable(). All are
		// pushed before BeginTable(): a scrolling table is a child window, which must end with the
		// style stack it began with.
		constexpr ImGuiCol TableColors[] = { ImGuiCol_TableHeaderBg, ImGuiCol_TableBorderStrong, ImGuiCol_TableBorderLight, ImGuiCol_TableRowBg,
			ImGuiCol_TableRowBgAlt, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive };

		// The painted tables being built (BeginTable()), innermost last, and the row of each
		// painted last
		struct PaintedTable
		{
			ImGuiTable* Table = nullptr;
			int PaintedRow = -1;
		};

		std::vector<PaintedTable>& GetPaintedTables()
		{
			static std::vector<PaintedTable> tables;
			return tables;
		}

		PaintedTable* FindPaintedTable()
		{
			std::vector<PaintedTable>& tables = GetPaintedTables();
			return !tables.empty() && tables.back().Table == GImGui->CurrentTable ? &tables.back() : nullptr;
		}

		// Paints the table's open row: its header strip, or its stripe. Its cells are all there,
		// so its bottom is where ImGui will end it: the lowest cell, as TableEndCell() measures it,
		// the open cell included. A row ImGui has ended already isn't painted; its bottom may
		// have moved since (the clipper moves it).
		void PaintOpenTableRow(PaintedTable& painted)
		{
			ImGuiTable& table = *painted.Table;
			if (!table.IsInsideRow || table.HostSkipItems || painted.PaintedRow == table.CurrentRow)
			{
				return;
			}
			painted.PaintedRow = table.CurrentRow;

			float bottom = table.RowPosY2;
			if (table.CurrentColumn >= 0 && table.Columns[table.CurrentColumn].IsEnabled)
			{
				bottom = std::max(bottom, table.InnerWindow->DC.CursorMaxPos.y + table.RowCellPaddingY);
			}
			const float top = table.RowPosY1;
			const bool bHeader = (table.RowFlags & ImGuiTableRowFlags_Headers) != 0;
			const bool bStripe = !bHeader && (table.Flags & ImGuiTableFlags_RowBg) != 0 && (table.RowBgColorCounter & 1) != 0;
			if ((!bHeader && !bStripe) || bottom <= top || bottom < table.InnerClipRect.Min.y || top > table.InnerClipRect.Max.y)
			{
				return;
			}

			const Theme& theme = GetTheme();
			const ScopedTableBackground background(table);
			WindowPainter& painter = GetWindowPainter();
			const PainterRect row{ { table.WorkRect.Min.x, top }, { table.WorkRect.Max.x, bottom } };
			painter.AddRoundedRect(row, theme.FrameRounding, PainterStyle::Filled(bHeader ? theme.TableHeader : theme.TableRowStripe));
			if (!bHeader)
			{
				return;
			}
			// Separators between the header's cells, where ImGui's column borders are
			const float inset = std::floor((bottom - top) * 0.25f);
			for (int column = 0; column < table.ColumnsCount; ++column)
			{
				const ImGuiTableColumn& data = table.Columns[column];
				if (!data.IsEnabled || column == table.RightMostEnabledColumn)
				{
					continue;
				}
				const float x = std::floor(data.MaxX) + 0.5f;
				painter.AddLine({ x, top + inset }, { x, bottom - inset }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.Separator));
			}
		}

		// A selectable's pills: the selection's, the hover's over it, and an outline while it has
		// keyboard focus. Selectables lie side by side with no gap, so they keep RowPillInset from
		// each other.
		void PaintSelectablePills(WindowPainter& painter, const PainterRect& rect, float selected, const WidgetLook& look, const Theme& theme)
		{
			const PainterRect pill{ { rect.Min.x, rect.Min.y + theme.RowPillInset }, { rect.Max.x, rect.Max.y - theme.RowPillInset } };
			if (selected > 0.0f)
			{
				painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.RowSelected, selected), look, theme)));
			}
			const float hovered = std::max(look.Hovered, look.Pressed);
			if (hovered > 0.0f)
			{
				painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.MenuItemHovered, hovered), look, theme)));
			}
			if (look.Focused > 0.0f)
			{
				painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Outlined(Fade(theme.Accent, look.Focused), 1.0f));
			}
		}

		// A selectable's label, cut where ImGui would clip it
		void PaintSelectableLabel(WindowPainter& painter, const PainterRect& rect, glm::vec2 labelPosition, std::string_view label, const WidgetLook& look, const Theme& theme)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float right = rect.Max.x - (style.ItemSpacing.x - std::floor(style.ItemSpacing.x * 0.5f));
			std::string cut;
			PaintText(painter, labelPosition, Shade(theme.Text, look, theme), FitText(label, right - labelPosition.x, cut));
		}

		enum class EFieldFrame
		{
			Plain,
			Search,
		};

		// Paints the frame of ImGui's field `label` where it goes next, before ImGui's widget
		// runs: at the cursor, the item width wide and a frame high, as ImGui's fields lay
		// themselves out, or `size` large
		void PaintNextFieldFrame(ImGuiWindow& window, const char* label, EFieldFrame kind, const Theme& theme, std::optional<glm::vec2> size = std::nullopt)
		{
			const ImRect frame = RectAtCursor(window, size.value_or(glm::vec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight())));
			if (!ImGui::IsRectVisible(frame.Min, frame.Max))
			{
				return;
			}
			const FieldState state = AnimateFieldState(window.GetID(label), theme);
			if (kind == EFieldFrame::Search)
			{
				PaintSearchField(GetWindowPainter(), ToPainterRect(frame), state.Editing, state.Look);
			}
			else
			{
				PaintField(GetWindowPainter(), ToPainterRect(frame), state.Editing, state.Look);
			}
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

	namespace
	{
		struct ButtonState
		{
			ImRect Rect;
			ImGuiID Id = 0;
			bool bClicked = false;
			WidgetLook Look;
		};

		// A button's layout and behaviour, as ImGui::ButtonEx(); no ID when it is clipped away.
		// `fitted`: its size where `sizeArgument` is 0.
		ButtonState AddButton(ImGuiWindow& window, const char* label, const glm::vec2& sizeArgument, const glm::vec2& fitted)
		{
			ButtonState button;
			const ImVec2 size = ImGui::CalcItemSize(ImVec2(sizeArgument.x, sizeArgument.y), fitted.x, fitted.y);
			button.Rect = RectAtCursor(window, glm::vec2(size.x, size.y));
			ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
			const ImGuiID id = window.GetID(label);
			if (!ImGui::ItemAdd(button.Rect, id))
			{
				return button;
			}

			bool bHovered = false;
			bool bHeld = false;
			button.Id = id;
			button.bClicked = ImGui::ButtonBehavior(button.Rect, id, &bHovered, &bHeld);
			button.Look = AnimateLook(id, bHovered, bHeld && bHovered, GetTheme());
			return button;
		}

		ButtonState AddButton(ImGuiWindow& window, const char* label, const glm::vec2& sizeArgument)
		{
			return AddButton(window, label, sizeArgument, CalcButtonSize(label));
		}

		// Where a document tab's close button is: a square at its right end
		ImRect GetDocumentTabCloseRect(const PainterRect& rect)
		{
			const float height = rect.GetHeight();
			const float size = std::floor(height * 0.6f);
			const float right = rect.Max.x - std::floor(height * 0.2f);
			const float top = std::floor((rect.Min.y + rect.Max.y - size) * 0.5f);
			return ImRect(ImVec2(right - size, top), ImVec2(right, top + size));
		}

		// Adds an item that takes no input, `size` large at the cursor; false when it is clipped away
		bool AddPlainItem(ImGuiWindow& window, glm::vec2 size, ImRect& outRect)
		{
			outRect = RectAtCursor(window, size);
			ImGui::ItemSize(ImVec2(size.x, size.y));
			return ImGui::ItemAdd(outRect, 0);
		}
	}

	bool Button(const char* label, const glm::vec2& size, EButtonTone tone)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ButtonState button = AddButton(*window, label, size);
		if (button.Id != 0)
		{
			PaintButton(GetWindowPainter(), ToPainterRect(button.Rect), VisibleLabel(label), button.Look, tone);
		}
		return button.bClicked;
	}

	bool MenuBarButton(const char* label, const glm::vec2& size, bool bOpen)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ButtonState button = AddButton(*window, label, size);
		if (button.Id != 0)
		{
			const float open = Animate(ImHashStr("Open", 0, button.Id), bOpen ? 1.0f : 0.0f, GetTheme().AnimationSpeed);
			PaintMenuBarButton(GetWindowPainter(), ToPainterRect(button.Rect), VisibleLabel(label), open, button.Look);
		}
		return button.bClicked;
	}

	bool WindowControlButton(const char* id, EWindowControl control, const glm::vec2& size)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ButtonState button = AddButton(*window, id, size);
		if (button.Id != 0)
		{
			PaintWindowControl(GetWindowPainter(), ToPainterRect(button.Rect), control, button.Look);
		}
		return button.bClicked;
	}

	bool IconButton(const char* label, EIcon icon, const glm::vec2& size, EButtonTone tone, bool bDropdown)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ButtonState button = AddButton(*window, label, size, CalcIconButtonSize(label, icon, bDropdown, size.y));
		if (button.Id != 0)
		{
			PaintIconButton(GetWindowPainter(), ToPainterRect(button.Rect), VisibleLabel(label), icon, button.Look, tone, bDropdown);
		}
		return button.bClicked;
	}

	bool BarTab(const char* label, bool bSelected, const glm::vec2& size)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ButtonState button = AddButton(*window, label, size, CalcBarTabSize(label, size.y));
		if (button.Id != 0)
		{
			const float selected = Animate(ImHashStr("Selected", 0, button.Id), bSelected ? 1.0f : 0.0f, GetTheme().AnimationSpeed);
			PaintBarTab(GetWindowPainter(), ToPainterRect(button.Rect), VisibleLabel(label), selected, button.Look);
		}
		return button.bClicked;
	}

	DocumentTabResult DocumentTab(const char* label, bool bCanClose, const glm::vec2& size)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		DocumentTabResult result;
		if (window->SkipItems)
		{
			return result;
		}
		const Theme& theme = GetTheme();

		const glm::vec2 fitted = CalcDocumentTabSize(label, size.y);
		const ImVec2 tabSize = ImGui::CalcItemSize(ImVec2(size.x, size.y), fitted.x, fitted.y);
		const ImRect rect = RectAtCursor(*window, glm::vec2(tabSize.x, tabSize.y));
		const ImGuiID id = window->GetID(label);

		// The close button first, so that it takes the mouse from the tab around it
		const ImRect closeRect = GetDocumentTabCloseRect(ToPainterRect(rect));
		const ImGuiID closeId = ImHashStr("##Close", 0, id);
		WidgetLook closeLook{ 0.0f, 0.0f, 0.0f, !bCanClose };
		ImGui::BeginDisabled(!bCanClose);
		if (ImGui::ItemAdd(closeRect, closeId))
		{
			bool bHovered = false;
			bool bHeld = false;
			result.bCloseClicked = ImGui::ButtonBehavior(closeRect, closeId, &bHovered, &bHeld);
			closeLook = AnimateLook(closeId, bHovered, bHeld && bHovered, theme);
		}
		ImGui::EndDisabled();

		// The tab, which IsItemHovered() and the like then ask about
		ImGui::ItemSize(tabSize, ImGui::GetStyle().FramePadding.y);
		if (!ImGui::ItemAdd(rect, id))
		{
			return result;
		}
		bool bHovered = false;
		bool bHeld = false;
		result.bClicked = ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld);
		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		PaintDocumentTab(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), look, closeLook);
		return result;
	}

	void BarDivider(float height)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}

		ImRect rect;
		if (AddPlainItem(*window, glm::vec2(1.0f, height), rect))
		{
			const Theme& theme = GetTheme();
			const float lineHeight = std::floor(height * theme.BarDividerHeight);
			const float top = std::floor(rect.Min.y + (height - lineHeight) * 0.5f);
			GetWindowPainter().AddRect(PainterRect{ { rect.Min.x, top }, { rect.Min.x + 1.0f, top + lineHeight } }, PainterStyle::Filled(theme.BarDivider));
		}
	}

	void StatusItem(const char* text, EStatusIcon icon, float height)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}

		ImRect rect;
		if (AddPlainItem(*window, CalcStatusItemSize(text, icon, height), rect))
		{
			PaintStatusItem(GetWindowPainter(), ToPainterRect(rect), text, icon);
		}
	}

	void TitlebarLogo(const char* name, float height)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}

		ImRect rect;
		if (AddPlainItem(*window, CalcTitlebarLogoSize(name, height), rect))
		{
			PaintTitlebarLogo(GetWindowPainter(), ToPainterRect(rect), name);
		}
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

	bool SliderFloat(const char* label, float* value, float min, float max, const char* format, ImGuiSliderFlags flags)
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
		const bool bChanged = ImGui::SliderBehavior(track, id, ImGuiDataType_Float, value, &min, &max, format, flags, &grab);
		if (bChanged)
		{
			ImGui::MarkItemEdited(id);
		}

		char valueText[64];
		ImFormatString(valueText, sizeof(valueText), format, *value);
		// Where ImGui's grab is: by ratio on a logarithmic slider over a range above 0
		const bool bLogarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0 && min > 0.0f && max > min;
		const float fraction = bLogarithmic ? std::clamp(std::log(std::max(*value, min) / min) / std::log(max / min), 0.0f, 1.0f)
			: max > min						 ? std::clamp((*value - min) / (max - min), 0.0f, 1.0f)
											 : 0.0f;
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

	TreeRowResult TreeRow(const char* label, const TreeRowOptions& options)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return TreeRowResult{};
		}
		ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		// As ImGui::TreeNodeEx(): the open state is kept in the window's storage, per ID, and
		// SetNextItemOpen() sets it.
		const ImGuiID id = window->GetID(label);
		ImGuiStorage& storage = *window->DC.StateStorage;
		bool bOpen = !options.bLeaf && ImGui::TreeNodeUpdateNextOpen(id, options.bDefaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);

		// The row takes the whole width, whatever the indentation, so the pill spans it. For the
		// layout it is only as wide as what it shows, as ImGui's SpanAvailWidth tree nodes are, so
		// a column or window sized to its contents doesn't grow from it.
		const float contentX = window->DC.CursorPos.x;
		const float top = window->DC.CursorPos.y;
		const ImRect rect(ImVec2(window->WorkRect.Min.x, top), ImVec2(window->WorkRect.Max.x, top + CalcTreeRowHeight()));
		const float iconWidth = options.Icon != ETreeRowIcon::None ? TreeRowIconSize() + g.Style.ItemInnerSpacing.x : 0.0f;
		const float shownWidth = TreeRowArrowWidth() + iconWidth + CalcTextSize(VisibleLabel(label)).x;
		ImGui::ItemSize(ImVec2(shownWidth, rect.GetHeight()), 0.0f);
		if (!ImGui::ItemAdd(rect, id))
		{
			// Scrolled out of view, but its children still follow when it is open.
			if (bOpen)
			{
				ImGui::TreePushOverrideID(id);
			}
			return TreeRowResult{ false, bOpen };
		}

		bool bHovered = false;
		bool bHeld = false;
		const bool bPressed = ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld, ImGuiButtonFlags_PressedOnClick);
		const bool bByKeyboard = g.NavActivateId == id;
		const bool bOnArrow = g.IO.MousePos.x >= contentX && g.IO.MousePos.x < contentX + TreeRowArrowWidth();

		TreeRowResult result;
		bool bToggle = false;
		if (bPressed)
		{
			if (!options.bLeaf && (options.bToggleOnClick || (!bByKeyboard && bOnArrow)))
			{
				bToggle = true;
			}
			else
			{
				result.bClicked = true;
			}
		}
		// A double click on the label; on the arrow, or with bToggleOnClick, each click already toggles.
		if (!options.bLeaf && !options.bToggleOnClick && options.bDoubleClickToggles && bHovered && !bOnArrow &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			bToggle = true;
		}
		// As ImGui's tree nodes: Left closes an open row, Right opens a closed one, instead of moving.
		if (!options.bLeaf && g.NavId == id && ((g.NavMoveDir == ImGuiDir_Left && bOpen) || (g.NavMoveDir == ImGuiDir_Right && !bOpen)))
		{
			bToggle = true;
			ImGui::NavMoveRequestCancel();
		}
		if (bToggle)
		{
			bOpen = !bOpen;
			storage.SetBool(id, bOpen);
		}

		TreeRowLook row;
		row.ContentX = contentX;
		row.Depth = window->DC.TreeDepth;
		row.bLeaf = options.bLeaf;
		row.Open = Animate(ImHashStr("Open", 0, id), bOpen ? 1.0f : 0.0f, theme.AnimationSpeed);
		row.Selected = Animate(ImHashStr("Selected", 0, id), options.bSelected ? 1.0f : 0.0f, theme.AnimationSpeed);
		row.Icon = options.Icon;
		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		PaintTreeRow(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), row, look);

		if (bOpen)
		{
			ImGui::TreePushOverrideID(id);
		}
		result.bOpen = bOpen;
		return result;
	}

	void TreeRowEnd()
	{
		ImGui::TreePop();
	}

	float CalcTreeRowLabelOffset(ETreeRowIcon icon)
	{
		return TreeRowArrowWidth() + (icon != ETreeRowIcon::None ? TreeRowIconSize() + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f);
	}

	bool CollapsingHeader(const char* label, bool bDefaultOpen)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		// As ImGui::CollapsingHeader(): the open state in the window's storage, under the label's ID
		const ImGuiID id = window->GetID(label);
		ImGuiStorage& storage = *window->DC.StateStorage;
		bool bOpen = storage.GetBool(id, bDefaultOpen);

		// Across the window, as wide as what it shows for the layout, as tree rows are
		const float top = window->DC.CursorPos.y;
		const ImRect rect(ImVec2(window->WorkRect.Min.x, top), ImVec2(window->WorkRect.Max.x, top + ImGui::GetFrameHeight()));
		const float shownWidth = theme.RowPillMargin + g.Style.FramePadding.x + TreeRowArrowWidth() + CalcTextSize(VisibleLabel(label)).x;
		ImGui::ItemSize(ImVec2(shownWidth, rect.GetHeight()), g.Style.FramePadding.y);
		if (!ImGui::ItemAdd(rect, id))
		{
			return bOpen;
		}

		bool bHovered = false;
		bool bHeld = false;
		bool bToggle = ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld);
		// As ImGui's tree nodes: Left closes it, Right opens it, instead of moving.
		if (g.NavId == id && ((g.NavMoveDir == ImGuiDir_Left && bOpen) || (g.NavMoveDir == ImGuiDir_Right && !bOpen)))
		{
			bToggle = true;
			ImGui::NavMoveRequestCancel();
		}
		if (bToggle)
		{
			bOpen = !bOpen;
			storage.SetBool(id, bOpen);
		}

		const float open = Animate(ImHashStr("Open", 0, id), bOpen ? 1.0f : 0.0f, theme.AnimationSpeed);
		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		PaintCollapsingHeader(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), open, look);
		return bOpen;
	}

	bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags, const char* hint)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const Theme& theme = GetTheme();

		PaintNextFieldFrame(*window, label, EFieldFrame::Plain, theme);
		const ScopedFieldStyle style(theme);
		return hint != nullptr ? ImGui::InputTextWithHint(label, hint, value, flags) : ImGui::InputText(label, value, flags);
	}

	bool InputTextMultiline(const char* label, std::string* value, const glm::vec2& size, ImGuiInputTextFlags flags)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const Theme& theme = GetTheme();

		// As ImGui::InputTextEx() sizes a multi-line field. Its text is in a child window, drawn
		// after this one's painting, so over the frame.
		const ImVec2 frameSize = ImGui::CalcItemSize(ImVec2(size.x, size.y), ImGui::CalcItemWidth(), ImGui::GetFontSize() * 8.0f + ImGui::GetStyle().FramePadding.y * 2.0f);
		PaintNextFieldFrame(*window, label, EFieldFrame::Plain, theme, glm::vec2(frameSize.x, frameSize.y));
		const ScopedFieldStyle style(theme);
		return ImGui::InputTextMultiline(label, value, frameSize, flags);
	}

	bool SearchField(const char* label, std::string* value, const char* placeholder)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const Theme& theme = GetTheme();

		PaintNextFieldFrame(*window, label, EFieldFrame::Search, theme);
		const ScopedFieldStyle style(theme);
		// ImGui's text, and the placeholder, start after the magnifier.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(CalcSearchTextOffset(), ImGui::GetStyle().FramePadding.y));
		const bool bChanged = ImGui::InputTextWithHint(label, placeholder, value);
		ImGui::PopStyleVar();
		return bChanged;
	}

	bool DragScalar(const char* label, ImGuiDataType type, void* value, float speed, const char* format)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const Theme& theme = GetTheme();

		PaintNextFieldFrame(*window, label, EFieldFrame::Plain, theme);
		const ScopedFieldStyle style(theme);
		return ImGui::DragScalar(label, type, value, speed, nullptr, nullptr, format);
	}

	bool DragFloat(const char* label, float* value, float speed, const char* format)
	{
		return DragScalar(label, ImGuiDataType_Float, value, speed, format);
	}

	bool InputScalar(const char* label, ImGuiDataType type, void* value, const char* format, ImGuiInputTextFlags flags)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const Theme& theme = GetTheme();

		// Without step buttons, ImGui::InputScalar() is an InputText() under the same label.
		PaintNextFieldFrame(*window, label, EFieldFrame::Plain, theme);
		const ScopedFieldStyle style(theme);
		return ImGui::InputScalar(label, type, value, nullptr, nullptr, format, flags);
	}

	bool DragVector(const char* label, float* values, int count, float speed, const char* format)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		const Theme& theme = GetTheme();
		count = std::clamp(count, 1, 4);

		// As ImGui::DragScalarN(): one group, so the last-item queries after it cover every
		// component, and the components share the item width.
		bool bChanged = false;
		const float width = ImGui::CalcItemWidth();
		const float spacing = style.ItemInnerSpacing.x;
		const float componentWidth = std::max(1.0f, std::floor((width - spacing * static_cast<float>(count - 1)) / static_cast<float>(count)));
		const float tagWidth = CalcAxisTagWidth();
		ImGui::BeginGroup();
		ImGui::PushID(label);
		for (int component = 0; component < count; ++component)
		{
			ImGui::PushID(component);
			if (component > 0)
			{
				ImGui::SameLine(0.0f, spacing);
			}

			// The last component takes what rounding left over.
			const float frameWidth = component < count - 1 ? componentWidth : std::max(1.0f, width - (componentWidth + spacing) * static_cast<float>(count - 1));
			const ImRect frame = RectAtCursor(*window, glm::vec2(frameWidth, ImGui::GetFrameHeight()));
			if (ImGui::IsRectVisible(frame.Min, frame.Max))
			{
				const FieldState state = AnimateFieldState(window->GetID("##Value"), theme);
				PaintVectorComponent(GetWindowPainter(), ToPainterRect(frame), component, state.Editing, state.Look);
			}

			// ImGui's field after the axis letter, so its centred value never covers the letter
			window->DC.CursorPos.x += tagWidth;
			ImGui::SetNextItemWidth(std::max(1.0f, frameWidth - tagWidth));
			{
				const ScopedFieldStyle fieldStyle(theme);
				bChanged |= ImGui::DragFloat("##Value", &values[component], speed, 0.0f, 0.0f, format);
			}
			ImGui::PopID();
		}
		ImGui::PopID();

		const std::string_view shownLabel = VisibleLabel(label);
		if (!shownLabel.empty())
		{
			ImGui::SameLine(0.0f, spacing);
			ImGui::TextEx(shownLabel.data(), shownLabel.data() + shownLabel.size());
		}
		ImGui::EndGroup();
		return bChanged;
	}

	void BeginPropertyRow(const char* label)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}
		ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		const ImGuiID id = window->GetID(label);
		const float contentX = window->DC.CursorPos.x;
		const float top = window->DC.CursorPos.y;
		const ImRect rect(ImVec2(window->WorkRect.Min.x, top), ImVec2(window->WorkRect.Max.x, top + CalcTreeRowHeight()));
		const float valueX = CalcPropertyValueX(rect.Min.x, rect.Max.x);

		// The label's column is the row's item, for the layout and for IsItemHovered(). Not
		// reached by keyboard navigation: the value's fields are.
		const ImRect labelRect(ImVec2(contentX, top), ImVec2(std::max(valueX, contentX), rect.Max.y));
		ImGui::ItemSize(labelRect.GetSize(), g.Style.FramePadding.y);
		if (ImGui::ItemAdd(labelRect, id, nullptr, ImGuiItemFlags_NoNav))
		{
			// Hovered anywhere along the row, also while one of its fields is dragged
			const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(rect.Min, rect.Max);
			WidgetLook look;
			look.bDisabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
			look.Hovered = Animate(ImHashStr("Hovered", 0, id), bHovered ? 1.0f : 0.0f, theme.AnimationSpeed);

			PropertyRowLook row;
			row.ContentX = contentX;
			row.Depth = window->DC.TreeDepth;
			PaintPropertyRow(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), row, look);
		}

		// The value on the same line, from the value column to inside the hover pill's end
		ImGui::SameLine(0.0f, 0.0f);
		window->DC.CursorPos.x = valueX;
		ImGui::PushItemWidth(std::max(1.0f, rect.Max.x - theme.RowPillMargin * 2.0f - valueX));
	}

	void EndPropertyRow()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}

		ImGui::PopItemWidth();
		// Without a value, the cursor is still on the row's line.
		if (window->DC.IsSameLine)
		{
			ImGui::NewLine();
		}
	}

	bool Checkbox(const char* label, bool* value)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		// As ImGui::Checkbox(): the box and its label are one item.
		const ImGuiID id = window->GetID(label);
		const glm::vec2 size = CalcCheckboxSize(label);
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
		PaintCheckbox(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), on, look);
		return bClicked;
	}

	bool MenuItem(const char* label, const char* shortcut, bool bSelected, bool bEnabled)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		// Menu bars lay their items out in a row, which ImGui's own does.
		if (window->DC.LayoutType == ImGuiLayoutType_Horizontal)
		{
			return ImGui::MenuItem(label, shortcut, bSelected, bEnabled);
		}
		const ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		// As ImGui::MenuItemEx() in a menu: the columns' widths go to the menu's layout, so it is
		// wide enough for every item, and an invisible Selectable() across the menu is the
		// behaviour, under the same ID as ImGui's menu items.
		const std::string_view shownLabel = VisibleLabel(label);
		const std::string_view shownShortcut = shortcut != nullptr ? std::string_view(shortcut) : std::string_view();
		const float checkWidth = std::floor(g.FontSize * 1.2f);
		const float minWidth = window->DC.MenuColumns.DeclColumns(0.0f, CalcTextSize(shownLabel).x, shownShortcut.empty() ? 0.0f : CalcTextSize(shownShortcut).x, checkWidth);

		ImGui::PushID(label);
		ImGui::BeginDisabled(!bEnabled);
		bool bPressed = false;
		{
			const ScopedInvisibleItem invisible;
			const ImGuiSelectableFlags flags = ImGuiSelectableFlags_SelectOnRelease | ImGuiSelectableFlags_NoSetKeyOwner | ImGuiSelectableFlags_SetNavIdOnHover | ImGuiSelectableFlags_SpanAvailWidth;
			bPressed = ImGui::Selectable("", false, flags, ImVec2(minWidth, g.FontSize));
		}
		if (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)
		{
			const WidgetLook look = AnimateLastItemLook(theme, true);
			PaintMenuItem(GetWindowPainter(), ToPainterRect(g.LastItemData.Rect), shownLabel, shownShortcut, bSelected, look);
		}
		ImGui::EndDisabled();
		ImGui::PopID();
		return bPressed;
	}

	bool MenuItem(const char* label, const char* shortcut, bool* selected, bool bEnabled)
	{
		if (MenuItem(label, shortcut, selected != nullptr && *selected, bEnabled))
		{
			if (selected != nullptr)
			{
				*selected = !*selected;
			}
			return true;
		}
		return false;
	}

	bool Selectable(const char* label, bool bSelected, ImGuiSelectableFlags flags, const glm::vec2& size)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}
		const ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		// Where ImGui puts the label: at the cursor, on the line's text baseline
		const glm::vec2 labelPosition(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
		const ImGuiID id = window->GetID(label);
		bool bPressed = false;
		{
			const ScopedInvisibleItem invisible;
			bPressed = ImGui::Selectable(label, bSelected, flags, ImVec2(size.x, size.y));
		}
		if (g.LastItemData.ID == id && (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible))
		{
			const WidgetLook look = AnimateLastItemLook(theme, false);
			const float selected = Animate(ImHashStr("Selected", 0, id), bSelected ? 1.0f : 0.0f, theme.AnimationSpeed);
			const PainterRect rect = ToPainterRect(g.LastItemData.Rect);
			WindowPainter& painter = GetWindowPainter();
			if ((flags & ImGuiSelectableFlags_SpanAllColumns) != 0 && g.CurrentTable != nullptr)
			{
				// Across the row, behind all its cells, as ImGui draws its highlight there; the
				// label stays in its cell.
				{
					const ScopedTableBackground background(*g.CurrentTable);
					PaintSelectablePills(painter, rect, selected, look, theme);
				}
				PaintSelectableLabel(painter, rect, labelPosition, VisibleLabel(label), look, theme);
			}
			else
			{
				PaintSelectable(painter, rect, labelPosition, VisibleLabel(label), selected, look);
			}
		}
		return bPressed;
	}

	void Separator()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return;
		}
		// Menu bars' upright separators, and the old Columns(), keep ImGui's own.
		if (window->DC.LayoutType == ImGuiLayoutType_Horizontal || window->DC.CurrentColumns != nullptr)
		{
			ImGui::Separator();
			return;
		}

		// As ImGui's: from the cursor to the work rect's end, its thickness high, and not reached by
		// the keyboard. Its width isn't given to the layout, so a window that fits its contents
		// doesn't grow from it.
		const float thickness = ImGui::GetStyle().SeparatorSize;
		const ImRect rect(window->DC.CursorPos, ImVec2(window->WorkRect.Max.x, window->DC.CursorPos.y + thickness));
		ImGui::ItemSize(ImVec2(0.0f, thickness));
		if (ImGui::ItemAdd(rect, 0))
		{
			const float y = (rect.Min.y + rect.Max.y) * 0.5f;
			GetWindowPainter().AddLine({ rect.Min.x, y }, { rect.Max.x, y }, thickness, EPainterLineCap::Flat, PainterStyle::Filled(GetTheme().Separator));
		}
	}

	bool BeginCombo(const char* label, const char* preview)
	{
		ImGuiContext& g = *GImGui;
		ImGuiWindow* window = ImGui::GetCurrentWindow();

		// As ImGui::BeginCombo(): the next window's settings are for the combo's popup.
		const ImGuiNextWindowDataFlags nextWindowFlags = g.NextWindowData.HasFlags;
		g.NextWindowData.ClearFlags();
		if (window->SkipItems)
		{
			return false;
		}
		const ImGuiStyle& style = g.Style;
		const Theme& theme = GetTheme();

		const ImGuiID id = window->GetID(label);
		const std::string_view shownLabel = VisibleLabel(label);
		const float labelWidth = CalcTextSize(shownLabel).x;
		const ImRect frame = RectAtCursor(*window, glm::vec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight()));
		const ImRect total(frame.Min, ImVec2(frame.Max.x + (labelWidth > 0.0f ? style.ItemInnerSpacing.x + labelWidth : 0.0f), frame.Max.y));
		ImGui::ItemSize(total, style.FramePadding.y);
		if (!ImGui::ItemAdd(total, id, &frame))
		{
			return false;
		}

		bool bHovered = false;
		bool bHeld = false;
		const bool bPressed = ImGui::ButtonBehavior(frame, id, &bHovered, &bHeld);
		const ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
		bool bPopupOpen = ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None);
		if (bPressed && !bPopupOpen)
		{
			ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None);
			bPopupOpen = true;
		}

		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		const float open = Animate(ImHashStr("Open", 0, id), bPopupOpen ? 1.0f : 0.0f, theme.AnimationSpeed);
		WindowPainter& painter = GetWindowPainter();
		PaintCombo(painter, ToPainterRect(frame), preview != nullptr ? std::string_view(preview) : std::string_view(), open, look);
		if (labelWidth > 0.0f)
		{
			PaintText(painter, { frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), shownLabel);
		}

		if (!bPopupOpen)
		{
			return false;
		}
		// ImGui's popup for it, placed and sized as ImGui's combos' are
		g.NextWindowData.HasFlags = nextWindowFlags;
		return ImGui::BeginComboPopup(popupId, frame, ImGuiComboFlags_None);
	}

	void EndCombo()
	{
		ImGui::EndCombo();
	}

	bool Combo(const char* label, int* current, std::span<const char* const> items)
	{
		const bool bInRange = *current >= 0 && *current < static_cast<int>(items.size());
		if (!BeginCombo(label, bInRange ? items[static_cast<size_t>(*current)] : ""))
		{
			return false;
		}

		bool bChanged = false;
		for (int index = 0; index < static_cast<int>(items.size()); ++index)
		{
			ImGui::PushID(index);
			const bool bSelected = index == *current;
			if (Selectable(items[static_cast<size_t>(index)], bSelected) && !bSelected)
			{
				*current = index;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		EndCombo();
		if (bChanged)
		{
			ImGui::MarkItemEdited(GImGui->LastItemData.ID);
		}
		return bChanged;
	}

	ColorSwatchResult ColorSwatch(const char* label, float* rgb)
	{
		ColorSwatchResult result;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return result;
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		const Theme& theme = GetTheme();

		const ImGuiID id = window->GetID(label);
		const std::string_view shownLabel = VisibleLabel(label);
		const float labelWidth = CalcTextSize(shownLabel).x;
		const ImRect frame = RectAtCursor(*window, glm::vec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight()));
		const ImRect total(frame.Min, ImVec2(frame.Max.x + (labelWidth > 0.0f ? style.ItemInnerSpacing.x + labelWidth : 0.0f), frame.Max.y));
		ImGui::ItemSize(total, style.FramePadding.y);
		// The picker stays open while the swatch is scrolled out of view.
		const bool bVisible = ImGui::ItemAdd(total, id, &frame);

		bool bHovered = false;
		bool bHeld = false;
		const ImGuiID popupId = ImHashStr("##ColorPopup", 0, id);
		if (bVisible && ImGui::ButtonBehavior(frame, id, &bHovered, &bHeld))
		{
			// Under the swatch, as ImGui's colour edits open their pickers
			ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None);
			ImGui::SetNextWindowPos(ImVec2(frame.Min.x, frame.Max.y + style.ItemSpacing.y));
		}

		if (bVisible)
		{
			const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
			WindowPainter& painter = GetWindowPainter();
			PaintColorSwatch(painter, ToPainterRect(frame), glm::vec3(rgb[0], rgb[1], rgb[2]), look);
			if (labelWidth > 0.0f)
			{
				PaintText(painter, { frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), shownLabel);
			}
		}

		if (ImGui::BeginPopupEx(popupId, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
		{
			// The picker is one group, so these cover all its parts.
			result.bChanged = ImGui::ColorPicker3("##Picker", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
			result.bEditStarted = ImGui::IsItemActivated();
			result.bEditEnded = ImGui::IsItemDeactivatedAfterEdit();
			ImGui::EndPopup();
		}
		return result;
	}

	bool BeginTable(const char* id, int columns, ImGuiTableFlags flags, const glm::vec2& outerSize, float innerWidth)
	{
		for (const ImGuiCol color : TableColors)
		{
			ImGui::PushStyleColor(color, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		if (!ImGui::BeginTable(id, columns, flags, ImVec2(outerSize.x, outerSize.y), innerWidth))
		{
			ImGui::PopStyleColor(static_cast<int>(std::size(TableColors)));
			return false;
		}
		GetPaintedTables().push_back(PaintedTable{ GImGui->CurrentTable, -1 });
		return true;
	}

	void TableNextRow(ImGuiTableRowFlags flags, float minHeight)
	{
		if (PaintedTable* painted = FindPaintedTable())
		{
			PaintOpenTableRow(*painted);
		}
		ImGui::TableNextRow(flags, minHeight);
	}

	void TableHeadersRow()
	{
		PaintedTable* painted = FindPaintedTable();
		if (painted != nullptr)
		{
			PaintOpenTableRow(*painted);
		}

		// ImGui's headers keep their behaviour and labels; a hovered or clicked header cell's
		// highlight is transparent.
		for (const ImGuiCol color : { ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive })
		{
			ImGui::PushStyleColor(color, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		ImGui::TableHeadersRow();
		ImGui::PopStyleColor(3);

		// Painted now, while it is still open: with frozen rows, ending the header row moves the
		// row's place and the background channel to the scrolling rows'.
		if (painted != nullptr)
		{
			PaintOpenTableRow(*painted);
		}
	}

	bool TableClipperStep(ImGuiListClipper& clipper)
	{
		if (PaintedTable* painted = FindPaintedTable())
		{
			PaintOpenTableRow(*painted);
		}
		return clipper.Step();
	}

	void EndTable()
	{
		std::vector<PaintedTable>& tables = GetPaintedTables();
		ImGuiTable* table = GImGui->CurrentTable;
		if (PaintedTable* painted = FindPaintedTable())
		{
			PaintOpenTableRow(*painted);
		}

		ImGui::EndTable();
		ImGui::PopStyleColor(static_cast<int>(std::size(TableColors)));
		if (!tables.empty() && tables.back().Table == table)
		{
			tables.pop_back();
		}

		// The border being dragged, or hovered long enough to show it can be, as an accent line
		// down the table, over it
		if (table == nullptr || table->HostSkipItems)
		{
			return;
		}
		const int border = table->ResizedColumn != -1 ? table->ResizedColumn : table->HoveredColumnBorder;
		if (border < 0 || border >= table->ColumnsCount || !table->Columns[border].IsEnabled)
		{
			return;
		}
		const Theme& theme = GetTheme();
		const float x = table->Columns[border].MaxX;
		WindowPainter& painter = GetWindowPainter();
		ImGui::PushClipRect(table->OuterRect.Min, table->OuterRect.Max, true);
		painter.AddLine({ x, table->OuterRect.Min.y }, { x, table->OuterRect.Max.y }, theme.SplitterWidth, EPainterLineCap::Round, PainterStyle::Filled(theme.Splitter));
		ImGui::PopClipRect();
	}

	AssetCardResult AssetCard(const char* label, const AssetCardOptions& options)
	{
		AssetCardResult result;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return result;
		}
		const ImGuiContext& g = *GImGui;
		const Theme& theme = GetTheme();

		const ImGuiID id = window->GetID(label);
		const glm::vec2 size = CalcAssetCardSize();
		const ImRect rect = RectAtCursor(*window, size);
		ImGui::ItemSize(ImVec2(size.x, size.y), 0.0f);
		if (!ImGui::ItemAdd(rect, id))
		{
			return result;
		}

		// A click selects at once; the second click of a double click opens too. By keyboard,
		// it selects and opens.
		bool bHovered = false;
		bool bHeld = false;
		const bool bByKeyboard = g.NavActivateId == id;
		if (ImGui::ButtonBehavior(rect, id, &bHovered, &bHeld, ImGuiButtonFlags_PressedOnClick))
		{
			result.bClicked = true;
			result.bOpened = bByKeyboard;
		}
		if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			result.bOpened = true;
		}

		AssetCardLook card;
		card.Icon = options.Icon;
		card.TypeLabel = options.TypeLabel;
		card.Thumbnail = options.Thumbnail;
		card.Selected = Animate(ImHashStr("Selected", 0, id), options.bSelected ? 1.0f : 0.0f, theme.AnimationSpeed);
		const WidgetLook look = AnimateLook(id, bHovered, bHeld && bHovered, theme);
		PaintAssetCard(GetWindowPainter(), ToPainterRect(rect), VisibleLabel(label), card, look);
		return result;
	}

	namespace
	{
		// A button's frame in its tone: the fill, with its shadow and focus glow, and the outline
		void PaintButtonFrame(WindowPainter& painter, const PainterRect& rect, const WidgetLook& look, EButtonTone tone, const Theme& theme)
		{
			struct ToneColors
			{
				const glm::vec4& Rest;
				const glm::vec4& Hovered;
				const glm::vec4& Pressed;
				const glm::vec4& Border;
			};
			const ToneColors colors = tone == EButtonTone::Go ? ToneColors{ theme.ButtonGo, theme.ButtonGoHovered, theme.ButtonGoPressed, theme.FrameBorder }
				: tone == EButtonTone::Stop	   ? ToneColors{ theme.ButtonStop, theme.ButtonStopHovered, theme.ButtonStopPressed, theme.FrameBorder }
				: tone == EButtonTone::Primary ? ToneColors{ theme.ButtonPrimary, theme.ButtonPrimaryHovered, theme.ButtonPrimaryPressed, theme.ButtonPrimaryBorder }
											   : ToneColors{ theme.Frame, theme.FrameHovered, theme.FramePressed, theme.FrameBorder };
			const glm::vec4 fill = glm::mix(glm::mix(colors.Rest, colors.Hovered, look.Hovered), colors.Pressed, look.Pressed);
			painter.AddRoundedRect(rect, theme.FrameRounding, Filled(Shade(fill, look, theme), ControlEffects(look, theme, true)));
			painter.AddRoundedRect(rect, theme.FrameRounding, PainterStyle::Outlined(Shade(colors.Border, look, theme), theme.FrameBorderWidth));
		}

		// A chevron pointing down, `size` wide, centred on `centre`
		void PaintChevron(WindowPainter& painter, glm::vec2 centre, float size, float width, const glm::vec4& color)
		{
			const glm::vec2 tip = centre + glm::vec2(0.0f, size * 0.25f);
			painter.AddLine(centre + glm::vec2(-size * 0.5f, -size * 0.25f), tip, width, EPainterLineCap::Round, PainterStyle::Filled(color));
			painter.AddLine(tip, centre + glm::vec2(size * 0.5f, -size * 0.25f), width, EPainterLineCap::Round, PainterStyle::Filled(color));
		}

		// The room an icon button keeps for the chevron of a dropdown, at its right end
		float DropdownWidth(float height)
		{
			return std::floor(height * 0.8f);
		}

		// The pieces of a document tab, from its left: room, the icon, a gap, the name; and the
		// room the close button takes at its right end (GetDocumentTabCloseRect())
		struct DocumentTabLayout
		{
			float Padding = 0.0f;
			float IconSize = 0.0f;
			float Gap = 0.0f;
			float CloseWidth = 0.0f;
		};

		DocumentTabLayout GetDocumentTabLayout(float height, const Theme& theme)
		{
			return DocumentTabLayout{ std::floor(height * 0.4f), std::floor(height * theme.IconSize), std::floor(height * 0.35f),
				std::floor(height * 0.8f) + std::floor(height * 0.35f) };
		}

		// A status item's round icon, this much of the item's height, and the gap after it
		constexpr float StatusIconFraction = 0.6f;
		constexpr float StatusIconGap = 0.3f;
		// The logo's circle, this much of the titlebar's height, and the gap after it
		constexpr float LogoFraction = 0.65f;
		constexpr float LogoGap = 0.3f;
	}

	void PaintButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, const WidgetLook& look, EButtonTone tone)
	{
		const Theme& theme = GetTheme();
		PaintButtonFrame(painter, rect, look, tone, theme);

		// The label centred, pushed down a little while pressed
		const glm::vec2 textSize = CalcTextSize(label);
		const glm::vec2 centre = (rect.Min + rect.Max) * 0.5f;
		const glm::vec2 textPosition = centre - textSize * 0.5f + glm::vec2(0.0f, theme.PressedTextOffset * look.Pressed);
		PaintText(painter, textPosition, Shade(theme.Text, look, theme), label);
	}

	void PaintMenuBarButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, float open, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();

		// The open menu's pill, and the hover's over it, in a little from the bar's top and bottom
		const PainterRect pill{ { rect.Min.x, rect.Min.y + theme.TabPillInset }, { rect.Max.x, rect.Max.y - theme.TabPillInset } };
		if (open > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.RowSelected, open), look, theme)));
		}
		const float hovered = std::max(look.Hovered, look.Pressed);
		if (hovered > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.MenuItemHovered, hovered), look, theme)));
		}
		if (look.Focused > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Outlined(Fade(theme.Accent, look.Focused), 1.0f));
		}

		const glm::vec2 textSize = CalcTextSize(label);
		const glm::vec2 centre = (rect.Min + rect.Max) * 0.5f;
		PaintText(painter, glm::floor(centre - textSize * 0.5f), Shade(theme.Text, look, theme), label);
	}

	void PaintWindowControl(WindowPainter& painter, const PainterRect& rect, EWindowControl control, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();

		// Filled edge to edge while hovered or pressed, as Windows' own
		const bool bClose = control == EWindowControl::Close;
		const glm::vec4& hoveredFill = bClose ? theme.WindowCloseHovered : theme.WindowControlHovered;
		const glm::vec4& pressedFill = bClose ? theme.WindowClosePressed : theme.WindowControlPressed;
		const float hovered = std::max(look.Hovered, look.Pressed);
		if (hovered > 0.0f)
		{
			const glm::vec4 fill = glm::mix(hoveredFill, pressedFill, look.Pressed);
			painter.AddRect(rect, PainterStyle::Filled(Shade(Fade(fill, hovered), look, theme)));
		}

		// The icon in a square in the middle, on whole pixels so its thin lines stay sharp
		const float size = theme.WindowControlIconSize;
		const float width = theme.WindowControlIconWidth;
		const glm::vec2 min = glm::floor((rect.Min + rect.Max - glm::vec2(size)) * 0.5f);
		const glm::vec2 max = min + glm::vec2(size);
		const PainterStyle line = PainterStyle::Filled(Shade(theme.Text, look, theme));
		const PainterStyle outline = PainterStyle::Outlined(Shade(theme.Text, look, theme), width);
		// Lines and outlines are centred on their edges: half their width in from the square's edge
		const float half = width * 0.5f;
		switch (control)
		{
		case EWindowControl::Minimize:
		{
			const float y = std::floor((min.y + max.y) * 0.5f) + half;
			painter.AddLine({ min.x, y }, { max.x, y }, width, EPainterLineCap::Flat, line);
			break;
		}
		case EWindowControl::Maximize:
			painter.AddRect(PainterRect{ min + half, max - half }, outline);
			break;
		case EWindowControl::Restore:
		{
			// Two windows, the front one lower left, the back one showing above and right of it
			const float offset = std::floor(size * 0.2f);
			painter.AddRect(PainterRect{ glm::vec2(min.x, min.y + offset) + half, glm::vec2(max.x - offset, max.y) - half }, outline);
			painter.AddLine({ min.x + offset + half, min.y + offset }, { min.x + offset + half, min.y + half }, width, EPainterLineCap::Flat, line);
			painter.AddLine({ min.x + offset, min.y + half }, { max.x - half, min.y + half }, width, EPainterLineCap::Flat, line);
			painter.AddLine({ max.x - half, min.y }, { max.x - half, max.y - offset }, width, EPainterLineCap::Flat, line);
			painter.AddLine({ max.x - half, max.y - offset - half }, { max.x - offset, max.y - offset - half }, width, EPainterLineCap::Flat, line);
			break;
		}
		case EWindowControl::Close:
			painter.AddLine(min, max, width, EPainterLineCap::Round, line);
			painter.AddLine({ max.x, min.y }, { min.x, max.y }, width, EPainterLineCap::Round, line);
			break;
		}
	}

	void PaintIconButton(WindowPainter& painter, const PainterRect& rect, std::string_view label, EIcon icon, const WidgetLook& look, EButtonTone tone, bool bDropdown)
	{
		const Theme& theme = GetTheme();
		PaintButtonFrame(painter, rect, look, tone, theme);

		// The icon and the label after it, centred in the room left of the dropdown's, pushed
		// down a little while pressed
		const float height = rect.GetHeight();
		const float dropdownWidth = bDropdown ? DropdownWidth(height) : 0.0f;
		const float iconSize = icon != EIcon::None ? std::floor(height * theme.IconSize) : 0.0f;
		const float labelWidth = label.empty() ? 0.0f : CalcTextSize(label).x;
		const float gap = iconSize > 0.0f && labelWidth > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
		const float contentWidth = iconSize + gap + labelWidth;
		const float left = std::floor(rect.Min.x + (rect.GetWidth() - dropdownWidth - contentWidth) * 0.5f);
		const float middle = (rect.Min.y + rect.Max.y) * 0.5f + theme.PressedTextOffset * look.Pressed;
		const glm::vec4 color = Shade(theme.Text, look, theme);
		if (iconSize > 0.0f)
		{
			const glm::vec2 iconMin{ left, std::floor(middle - iconSize * 0.5f) };
			PaintIcon(painter, PainterRect{ iconMin, iconMin + glm::vec2(iconSize) }, icon, color);
		}
		if (labelWidth > 0.0f)
		{
			PaintText(painter, glm::floor(glm::vec2(left + iconSize + gap, middle - ImGui::GetFontSize() * 0.5f)), color, label);
		}

		// The dropdown's chevron, behind a thin line
		if (bDropdown)
		{
			const float lineX = std::floor(rect.Max.x - dropdownWidth);
			const float lineInset = std::floor(height * 0.25f);
			painter.AddRect(PainterRect{ { lineX, rect.Min.y + lineInset }, { lineX + 1.0f, rect.Max.y - lineInset } }, PainterStyle::Filled(Shade(theme.Separator, look, theme)));
			PaintChevron(painter, { lineX + dropdownWidth * 0.5f, middle }, std::floor(height * 0.3f), std::max(1.25f, height * 0.05f), Shade(theme.TextMuted, look, theme));
		}
	}

	void PaintIcon(WindowPainter& painter, const PainterRect& box, EIcon icon, const glm::vec4& color)
	{
		const float size = box.GetWidth();
		const PainterStyle fill = PainterStyle::Filled(color);
		// A point of the box, from (0, 0) at its top left to (1, 1) at its bottom right, and a rect between two
		const auto at = [&](float x, float y)
		{
			return box.Min + glm::vec2(x, y) * size;
		};
		const auto rect = [&](float left, float top, float right, float bottom)
		{
			return PainterRect{ at(left, top), at(right, bottom) };
		};
		switch (icon)
		{
		case EIcon::None:
			break;
		case EIcon::Play:
			painter.AddTriangle(at(0.2f, 0.08f), at(0.2f, 0.92f), at(0.92f, 0.5f), fill);
			break;
		case EIcon::PlayAll:
			painter.AddTriangle(at(0.0f, 0.14f), at(0.0f, 0.86f), at(0.56f, 0.5f), fill);
			painter.AddTriangle(at(0.44f, 0.14f), at(0.44f, 0.86f), at(1.0f, 0.5f), fill);
			break;
		case EIcon::Stop:
			painter.AddRoundedRect(rect(0.12f, 0.12f, 0.88f, 0.88f), size * 0.12f, fill);
			break;
		case EIcon::Pause:
			painter.AddRoundedRect(rect(0.18f, 0.08f, 0.4f, 0.92f), size * 0.05f, fill);
			painter.AddRoundedRect(rect(0.6f, 0.08f, 0.82f, 0.92f), size * 0.05f, fill);
			break;
		case EIcon::Step:
			painter.AddTriangle(at(0.12f, 0.1f), at(0.12f, 0.9f), at(0.7f, 0.5f), fill);
			painter.AddRoundedRect(rect(0.74f, 0.1f, 0.9f, 0.9f), size * 0.04f, fill);
			break;
		case EIcon::Build:
			// A hammer: the head across the top, the handle down from its middle
			painter.AddRoundedRect(rect(0.1f, 0.1f, 0.9f, 0.38f), size * 0.06f, fill);
			painter.AddRoundedRect(rect(0.41f, 0.38f, 0.59f, 0.94f), size * 0.06f, fill);
			break;
		case EIcon::Document:
		{
			// A page with two lines of text
			const float width = std::max(1.0f, std::floor(size * 0.09f));
			painter.AddRoundedRect(rect(0.16f, 0.04f, 0.84f, 0.96f), size * 0.12f, PainterStyle::Outlined(color, width));
			painter.AddLine(at(0.34f, 0.42f), at(0.66f, 0.42f), width, EPainterLineCap::Round, fill);
			painter.AddLine(at(0.34f, 0.62f), at(0.66f, 0.62f), width, EPainterLineCap::Round, fill);
			break;
		}
		case EIcon::Eyedropper:
		{
			// A pipette: its bulb at the top right, its tube down to the tip at the bottom left
			painter.AddCircle(at(0.74f, 0.26f), size * 0.2f, fill);
			painter.AddLine(at(0.62f, 0.38f), at(0.14f, 0.86f), std::max(1.0f, std::floor(size * 0.16f)), EPainterLineCap::Round, fill);
			break;
		}
		case EIcon::Locate:
		{
			// A target: a ring, with a dot in its middle
			painter.AddCircle(at(0.5f, 0.5f), size * 0.38f, PainterStyle::Outlined(color, std::max(1.0f, std::floor(size * 0.1f))));
			painter.AddCircle(at(0.5f, 0.5f), size * 0.14f, fill);
			break;
		}
		case EIcon::Reset:
		{
			// An arrow turning back: three quarters of a ring from the top round to the left, its
			// head at the top pointing into the gap
			const float pi = 3.14159265f;
			painter.AddArc(at(0.5f, 0.56f), size * 0.3f, std::max(1.0f, std::floor(size * 0.11f)), -0.5f * pi, pi, fill);
			painter.AddTriangle(at(0.24f, 0.26f), at(0.54f, 0.06f), at(0.54f, 0.46f), fill);
			break;
		}
		case EIcon::Clear:
		{
			// A cross
			const float width = std::max(1.0f, std::floor(size * 0.12f));
			painter.AddLine(at(0.22f, 0.22f), at(0.78f, 0.78f), width, EPainterLineCap::Round, fill);
			painter.AddLine(at(0.78f, 0.22f), at(0.22f, 0.78f), width, EPainterLineCap::Round, fill);
			break;
		}
		}
	}

	void PaintBarTab(WindowPainter& painter, const PainterRect& rect, std::string_view label, float selected, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();

		// The hover pill behind a tab that isn't selected
		const float hovered = std::max(look.Hovered, look.Pressed) * (1.0f - selected);
		if (hovered > 0.0f)
		{
			const PainterRect pill{ { rect.Min.x, rect.Min.y + theme.TabPillInset }, { rect.Max.x, rect.Max.y - theme.TabPillInset } };
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.MenuItemHovered, hovered), look, theme)));
		}

		// The selected tab raised, with the underline along its bottom between its rounded corners
		if (selected > 0.0f)
		{
			painter.AddRoundedRect(rect, theme.BarTabRounding, PainterStyle::Filled(Shade(Fade(theme.BarTab, selected), look, theme)));
			painter.AddRoundedRect(rect, theme.BarTabRounding, PainterStyle::Outlined(Shade(Fade(theme.BarTabBorder, selected), look, theme), theme.FrameBorderWidth));
			const float inset = std::floor(theme.BarTabRounding * 0.5f);
			painter.AddRect(PainterRect{ { rect.Min.x + inset, rect.Max.y - theme.TabUnderlineWidth - 1.0f }, { rect.Max.x - inset, rect.Max.y - 1.0f } },
				PainterStyle::Filled(Shade(Fade(theme.TabUnderline, selected), look, theme)));
		}
		if (look.Focused > 0.0f)
		{
			painter.AddRoundedRect(rect, theme.BarTabRounding, PainterStyle::Outlined(Fade(theme.Accent, look.Focused), 1.0f));
		}

		const glm::vec2 textSize = CalcTextSize(label);
		PaintText(painter, glm::floor((rect.Min + rect.Max - textSize) * 0.5f), Shade(theme.Text, look, theme), label);
	}

	void PaintDocumentTab(WindowPainter& painter, const PainterRect& rect, std::string_view label, const WidgetLook& look, const WidgetLook& closeLook)
	{
		const Theme& theme = GetTheme();
		const float height = rect.GetHeight();
		const DocumentTabLayout layout = GetDocumentTabLayout(height, theme);

		// A raised tab, rounded at the top, standing on the bar's bottom; lit a little while hovered
		const PainterCornerRadii radii{ theme.BarTabRounding, theme.BarTabRounding, 0.0f, 0.0f };
		painter.AddRoundedRect(rect, radii, Filled(Shade(theme.BarTab, look, theme), ControlEffects(look, theme, false)));
		const float hovered = std::max(look.Hovered, look.Pressed);
		if (hovered > 0.0f)
		{
			painter.AddRoundedRect(rect, radii, PainterStyle::Filled(Shade(Fade(theme.TabHovered, hovered), look, theme)));
		}
		painter.AddRoundedRect(rect, radii, PainterStyle::Outlined(Shade(theme.BarTabBorder, look, theme), theme.FrameBorderWidth));

		// The icon and the name, cut to fit before the close button
		const float middle = (rect.Min.y + rect.Max.y) * 0.5f;
		const glm::vec2 iconMin{ rect.Min.x + layout.Padding, std::floor(middle - layout.IconSize * 0.5f) };
		PaintIcon(painter, PainterRect{ iconMin, iconMin + glm::vec2(layout.IconSize) }, EIcon::Document, Shade(theme.TextMuted, look, theme));
		const float textLeft = iconMin.x + layout.IconSize + layout.Gap;
		std::string cut;
		PaintText(painter, glm::floor(glm::vec2(textLeft, middle - ImGui::GetFontSize() * 0.5f)), Shade(theme.Text, look, theme),
			FitText(label, rect.Max.x - layout.CloseWidth - textLeft, cut));

		// The close button: a cross, on the hover pill while hovered
		const PainterRect close = ToPainterRect(GetDocumentTabCloseRect(rect));
		const float closeHovered = std::max(closeLook.Hovered, closeLook.Pressed);
		if (closeHovered > 0.0f)
		{
			painter.AddRoundedRect(close, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.MenuItemHovered, closeHovered), closeLook, theme)));
		}
		const glm::vec2 centre = glm::floor((close.Min + close.Max) * 0.5f) + 0.5f;
		const float arm = std::floor(close.GetWidth() * 0.22f);
		const float width = theme.WindowControlIconWidth * 1.25f;
		const PainterStyle cross = PainterStyle::Filled(Shade(glm::mix(theme.TextMuted, theme.Text, closeHovered), closeLook, theme));
		painter.AddLine(centre - glm::vec2(arm), centre + glm::vec2(arm), width, EPainterLineCap::Round, cross);
		painter.AddLine(centre + glm::vec2(arm, -arm), centre + glm::vec2(-arm, arm), width, EPainterLineCap::Round, cross);
	}

	void PaintStatusItem(WindowPainter& painter, const PainterRect& rect, std::string_view text, EStatusIcon icon)
	{
		const Theme& theme = GetTheme();
		const float height = rect.GetHeight();
		const float middle = (rect.Min.y + rect.Max.y) * 0.5f;
		float textLeft = rect.Min.x;
		if (icon != EStatusIcon::None)
		{
			// A disc in the status's colour, with a mark on it: a tick, "!" or a cross
			const float size = std::floor(height * StatusIconFraction);
			const float top = std::floor(middle - size * 0.5f);
			const PainterRect box{ { rect.Min.x, top }, { rect.Min.x + size, top + size } };
			const glm::vec2 centre = (box.Min + box.Max) * 0.5f;
			const glm::vec4& color = icon == EStatusIcon::Ok ? theme.StatusOk : icon == EStatusIcon::Warning ? theme.StatusWarning : theme.StatusError;
			painter.AddCircle(centre, size * 0.5f, PainterStyle::Filled(color));
			const float width = std::max(1.5f, size * 0.12f);
			const PainterStyle mark = PainterStyle::Filled(theme.StatusIconMark);
			switch (icon)
			{
			case EStatusIcon::Ok:
				PaintCheckmark(painter, box, 1.0f, theme.StatusIconMark);
				break;
			case EStatusIcon::Warning:
				painter.AddLine(centre - glm::vec2(0.0f, size * 0.24f), centre + glm::vec2(0.0f, size * 0.06f), width, EPainterLineCap::Round, mark);
				painter.AddCircle(centre + glm::vec2(0.0f, size * 0.24f), width * 0.6f, mark);
				break;
			case EStatusIcon::Error:
				painter.AddLine(centre - glm::vec2(size * 0.18f), centre + glm::vec2(size * 0.18f), width, EPainterLineCap::Round, mark);
				painter.AddLine(centre + glm::vec2(size * 0.18f, -size * 0.18f), centre + glm::vec2(-size * 0.18f, size * 0.18f), width, EPainterLineCap::Round, mark);
				break;
			case EStatusIcon::None:
				break;
			}
			textLeft += size + std::floor(height * StatusIconGap);
		}
		PaintText(painter, glm::floor(glm::vec2(textLeft, middle - ImGui::GetFontSize() * 0.5f)), theme.Text, text);
	}

	void PaintTitlebarLogo(WindowPainter& painter, const PainterRect& rect, std::string_view name)
	{
		const Theme& theme = GetTheme();
		const float height = rect.GetHeight();
		const float size = std::floor(height * LogoFraction);
		const float middle = (rect.Min.y + rect.Max.y) * 0.5f;
		painter.AddCircle({ rect.Min.x + size * 0.5f, middle }, size * 0.5f, PainterStyle::Filled(theme.Logo));
		PaintText(painter, glm::floor(glm::vec2(rect.Min.x + size + std::floor(height * LogoGap), middle - ImGui::GetFontSize() * 0.5f)), theme.Text, name);
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

	void PaintTreeRow(WindowPainter& painter, const PainterRect& rect, std::string_view label, const TreeRowLook& row, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float centreY = (rect.Min.y + rect.Max.y) * 0.5f;

		// The pills: the selection's, then a hovered row's over it, and an outline while it has
		// keyboard focus
		const PainterRect pill = RowPill(rect, theme);
		if (row.Selected > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.RowSelected, row.Selected), look, theme)));
		}
		const float hovered = std::max(look.Hovered, look.Pressed);
		if (hovered > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.RowHovered, hovered), look, theme)));
		}
		if (look.Focused > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Outlined(Fade(theme.Accent, look.Focused), 1.0f));
		}

		PaintIndentGuides(painter, rect, row.ContentX, row.Depth, theme);

		// The arrow, turned down as the row opens. Leaves keep its room, so their labels line up
		// with their siblings'.
		const float arrowWidth = TreeRowArrowWidth();
		float x = row.ContentX;
		if (!row.bLeaf)
		{
			PaintArrow(painter, { x + arrowWidth * 0.5f, centreY }, arrowWidth, row.Open, Shade(theme.TextMuted, look, theme));
		}
		x += arrowWidth;

		if (row.Icon != ETreeRowIcon::None)
		{
			const float iconSize = TreeRowIconSize();
			const float iconTop = std::floor(centreY - iconSize * 0.5f);
			PaintTreeRowIcon(painter, row.Icon, PainterRect{ { x, iconTop }, { x + iconSize, iconTop + iconSize } }, look, theme);
			x += iconSize + style.ItemInnerSpacing.x;
		}

		PaintText(painter, { x, rect.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), label);
	}

	void PaintCollapsingHeader(WindowPainter& painter, const PainterRect& rect, std::string_view label, float open, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();

		// The pill, lighter when hovered, with an outline while it has keyboard focus
		const PainterRect pill{ { rect.Min.x + theme.RowPillMargin, rect.Min.y }, { rect.Max.x - theme.RowPillMargin, rect.Max.y } };
		const glm::vec4 fill = glm::mix(theme.Header, theme.HeaderHovered, std::max(look.Hovered, look.Pressed));
		painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(fill, look, theme)));
		if (look.Focused > 0.0f)
		{
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Outlined(Fade(theme.Accent, look.Focused), 1.0f));
		}

		const float arrowWidth = TreeRowArrowWidth();
		const float x = pill.Min.x + style.FramePadding.x;
		PaintArrow(painter, { x + arrowWidth * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f }, arrowWidth, open, Shade(theme.TextMuted, look, theme));
		PaintText(painter, { x + arrowWidth, rect.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), label);
	}

	void PaintField(WindowPainter& painter, const PainterRect& frame, float editing, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		PaintFieldFrame(painter, frame, theme.FrameRounding, editing, look, theme);
	}

	void PaintSearchField(WindowPainter& painter, const PainterRect& frame, float editing, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const float frameHeight = frame.GetHeight();
		PaintFieldFrame(painter, frame, theme.FrameRounding, editing, look, theme);

		// The magnifier: a ring, and a handle down to the right; brighter while edited
		const float size = SearchIconSize(frameHeight, theme);
		const glm::vec2 centre{ frame.Min.x + SearchIconLeft(frameHeight) + size * 0.42f, (frame.Min.y + frame.Max.y) * 0.5f - size * 0.08f };
		const float radius = size * 0.34f;
		const float stroke = std::max(1.25f, size * 0.12f);
		const glm::vec4 color = Shade(glm::mix(theme.TextMuted, theme.Text, editing), look, theme);
		painter.AddCircle(centre, radius, PainterStyle::Outlined(color, stroke));
		const glm::vec2 direction(0.7071f, 0.7071f);
		painter.AddLine(centre + direction * (radius + stroke * 0.5f), centre + direction * (radius + size * 0.36f), stroke, EPainterLineCap::Round, PainterStyle::Filled(color));
	}

	void PaintVectorComponent(WindowPainter& painter, const PainterRect& frame, int axis, float editing, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		PaintFieldFrame(painter, frame, theme.FrameRounding, editing, look, theme);

		// The axis letter, centred in its room at the frame's left
		constexpr std::string_view Letters[] = { "X", "Y", "Z", "W" };
		const glm::vec4 colors[] = { theme.AxisX, theme.AxisY, theme.AxisZ, theme.AxisW };
		const int index = std::clamp(axis, 0, 3);
		const std::string_view letter = Letters[index];
		const float x = std::floor(frame.Min.x + (CalcAxisTagWidth() - CalcTextSize(letter).x) * 0.5f);
		PaintText(painter, { x, frame.Min.y + ImGui::GetStyle().FramePadding.y }, Shade(colors[index], look, theme), letter);
	}

	void PaintPropertyRow(WindowPainter& painter, const PainterRect& rect, std::string_view label, const PropertyRowLook& row, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();

		if (look.Hovered > 0.0f)
		{
			painter.AddRoundedRect(RowPill(rect, theme), theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.RowHovered, look.Hovered), look, theme)));
		}
		PaintIndentGuides(painter, rect, row.ContentX, row.Depth, theme);

		// The label after a tree row's arrow's room, so it lines up with the tree rows' labels,
		// and cut to fit its column
		const float x = row.ContentX + TreeRowArrowWidth();
		const float right = CalcPropertyValueX(rect.Min.x, rect.Max.x) - style.ItemInnerSpacing.x;
		std::string cut;
		PaintText(painter, { x, rect.Min.y + style.FramePadding.y }, Shade(theme.PropertyLabel, look, theme), FitText(label, right - x, cut));
	}

	void PaintCheckmark(WindowPainter& painter, const PainterRect& box, float amount, const glm::vec4& color)
	{
		if (amount <= 0.0f)
		{
			return;
		}

		// Two strokes, the short one down to the corner, then the long one up; drawn in that order
		// as `amount` grows
		const float size = box.GetWidth();
		const float width = std::max(1.25f, size * GetTheme().CheckmarkWidth);
		const glm::vec2 start = box.Min + glm::vec2(0.24f, 0.53f) * size;
		const glm::vec2 corner = box.Min + glm::vec2(0.43f, 0.71f) * size;
		const glm::vec2 end = box.Min + glm::vec2(0.77f, 0.31f) * size;
		constexpr float ShortPart = 0.4f;
		const PainterStyle style = PainterStyle::Filled(color);
		painter.AddLine(start, glm::mix(start, corner, std::min(amount / ShortPart, 1.0f)), width, EPainterLineCap::Round, style);
		if (amount > ShortPart)
		{
			painter.AddLine(corner, glm::mix(corner, end, std::min((amount - ShortPart) / (1.0f - ShortPart), 1.0f)), width, EPainterLineCap::Round, style);
		}
	}

	void PaintCheckbox(WindowPainter& painter, const PainterRect& rect, std::string_view label, float on, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();

		// The box, centred on the frame's height, a little smaller while pressed
		const float boxSize = std::floor(rect.GetHeight() * theme.CheckboxSize);
		const float top = rect.Min.y + std::floor((rect.GetHeight() - boxSize) * 0.5f);
		const PainterRect box{ { rect.Min.x, top }, { rect.Min.x + boxSize, top + boxSize } };
		const glm::vec2 centre = (box.Min + box.Max) * 0.5f;
		const float scale = glm::mix(1.0f, theme.KnobPressedScale, look.Pressed);
		const PainterRect pressed{ centre + (box.Min - centre) * scale, centre + (box.Max - centre) * scale };

		// A field when off, the accent when on
		const glm::vec4 off = glm::mix(theme.Field, theme.FieldHovered, look.Hovered);
		const glm::vec4 onColor = glm::mix(theme.Accent, theme.AccentHovered, look.Hovered);
		painter.AddRoundedRect(pressed, theme.CheckboxRounding, Filled(Shade(glm::mix(off, onColor, on), look, theme), ControlEffects(look, theme, false)));
		painter.AddRoundedRect(pressed, theme.CheckboxRounding, PainterStyle::Outlined(Shade(Fade(theme.FieldBorder, 1.0f - on), look, theme), theme.FrameBorderWidth));
		PaintCheckmark(painter, pressed, on, Shade(theme.Knob, look, theme));

		if (!label.empty())
		{
			PaintText(painter, { box.Max.x + style.ItemInnerSpacing.x, rect.Min.y + style.FramePadding.y }, Shade(theme.Text, look, theme), label);
		}
	}

	void PaintMenuItem(WindowPainter& painter, const PainterRect& rect, std::string_view label, std::string_view shortcut, bool bChecked, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float fontSize = ImGui::GetFontSize();

		// The hover pill across the item; items lie side by side with no gap, so it keeps
		// RowPillInset from its neighbours'.
		const float hovered = std::max(look.Hovered, look.Pressed);
		if (hovered > 0.0f)
		{
			const PainterRect pill{ { rect.Min.x, rect.Min.y + theme.RowPillInset }, { rect.Max.x, rect.Max.y - theme.RowPillInset } };
			painter.AddRoundedRect(pill, theme.FrameRounding, PainterStyle::Filled(Shade(Fade(theme.MenuItemHovered, hovered), look, theme)));
		}

		// The text where ImGui's would be: half the item spacing in from the item's corner. The
		// tick's column is at the right end, and the shortcut right-aligned before it.
		const float left = rect.Min.x + std::floor(style.ItemSpacing.x * 0.5f);
		const float right = rect.Max.x - (style.ItemSpacing.x - std::floor(style.ItemSpacing.x * 0.5f));
		const float textY = rect.Min.y + std::floor(style.ItemSpacing.y * 0.5f);
		const float checkWidth = std::floor(fontSize * 1.2f);
		PaintText(painter, { left, textY }, Shade(theme.Text, look, theme), label);
		if (!shortcut.empty())
		{
			const float shortcutX = right - checkWidth - style.ItemSpacing.x - CalcTextSize(shortcut).x;
			PaintText(painter, { shortcutX, textY }, Shade(theme.TextMuted, look, theme), shortcut);
		}
		if (bChecked)
		{
			const float boxSize = std::floor(fontSize * 0.9f);
			const float boxLeft = right - checkWidth + std::floor((checkWidth - boxSize) * 0.5f);
			const float boxTop = textY + std::floor((fontSize - boxSize) * 0.5f);
			PaintCheckmark(painter, PainterRect{ { boxLeft, boxTop }, { boxLeft + boxSize, boxTop + boxSize } }, 1.0f, Shade(theme.Accent, look, theme));
		}
	}

	void PaintSelectable(WindowPainter& painter, const PainterRect& rect, glm::vec2 labelPosition, std::string_view label, float selected, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		PaintSelectablePills(painter, rect, selected, look, theme);
		PaintSelectableLabel(painter, rect, labelPosition, label, look, theme);
	}

	void PaintCombo(WindowPainter& painter, const PainterRect& frame, std::string_view preview, float open, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float frameHeight = frame.GetHeight();

		// A frame as a button's, which stays lit while the list is open
		const float lit = std::max(look.Hovered, open);
		const glm::vec4 fill = glm::mix(glm::mix(theme.Frame, theme.FrameHovered, lit), theme.FramePressed, look.Pressed);
		painter.AddRoundedRect(frame, theme.FrameRounding, Filled(Shade(fill, look, theme), ControlEffects(look, theme, true)));
		painter.AddRoundedRect(frame, theme.FrameRounding, PainterStyle::Outlined(Shade(theme.FrameBorder, look, theme), theme.FrameBorderWidth));

		// The chosen item's text, up to the chevron's room at the right end
		const float chevronLeft = frame.Max.x - frameHeight;
		std::string cut;
		PaintText(painter, frame.Min + glm::vec2(style.FramePadding.x, style.FramePadding.y), Shade(theme.Text, look, theme),
			FitText(preview, chevronLeft - frame.Min.x - style.FramePadding.x, cut));

		// The chevron, pointing down, turned to point up while the list is open
		const glm::vec2 centre{ chevronLeft + frameHeight * 0.5f, (frame.Min.y + frame.Max.y) * 0.5f };
		const float size = frameHeight * 0.16f;
		const float angle = open * 3.1415927f;
		const glm::vec4 color = Shade(glm::mix(theme.TextMuted, theme.Text, lit), look, theme);
		const glm::vec2 tip = Turned(centre, { 0.0f, size * 0.5f }, angle);
		const float width = std::max(1.25f, frameHeight * 0.07f);
		painter.AddLine(Turned(centre, { -size, -size * 0.5f }, angle), tip, width, EPainterLineCap::Round, PainterStyle::Filled(color));
		painter.AddLine(tip, Turned(centre, { size, -size * 0.5f }, angle), width, EPainterLineCap::Round, PainterStyle::Filled(color));
	}

	void PaintAssetCard(WindowPainter& painter, const PainterRect& rect, std::string_view label, const AssetCardLook& card, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		const float fontSize = ImGui::GetFontSize();
		const float padding = theme.AssetCardPadding;

		// The card: lit while hovered, in the selection's colour with an accent outline while
		// selected, and outlined while it has keyboard focus
		const glm::vec4 fill = glm::mix(theme.AssetCard, theme.AssetCardHovered, std::max(look.Hovered, look.Pressed));
		painter.AddRoundedRect(rect, theme.CardRounding, PainterStyle::Filled(Shade(fill, look, theme)));
		if (card.Selected > 0.0f)
		{
			painter.AddRoundedRect(rect, theme.CardRounding, PainterStyle::Filled(Shade(Fade(theme.RowSelected, card.Selected), look, theme)));
		}
		const float outline = std::max(card.Selected * 0.8f, look.Focused);
		if (outline > 0.0f)
		{
			painter.AddRoundedRect(rect, theme.CardRounding, PainterStyle::Outlined(Shade(Fade(theme.Accent, outline), look, theme), 1.0f));
		}

		// The picture: a square at the top
		const float pictureSize = rect.GetWidth() - padding * 2.0f;
		const PainterRect picture{ rect.Min + glm::vec2(padding), rect.Min + glm::vec2(padding + pictureSize) };
		const glm::vec2 centre = (picture.Min + picture.Max) * 0.5f;
		if (card.Thumbnail._TexData != nullptr || card.Thumbnail._TexID != ImTextureID_Invalid)
		{
			painter.AddImage(card.Thumbnail, picture, PainterStyle::Filled(Shade(glm::vec4(1.0f), look, theme)));
		}
		else if (card.Icon == EAssetCardIcon::Folder)
		{
			const float iconSize = std::floor(pictureSize * 0.62f);
			const glm::vec2 iconMin = glm::floor(centre - glm::vec2(iconSize * 0.5f));
			PaintTreeRowIcon(painter, ETreeRowIcon::Folder, PainterRect{ iconMin, iconMin + glm::vec2(iconSize) }, look, theme);
		}
		else
		{
			// A page with its top right corner folded, over the type's name
			const float lift = card.TypeLabel.empty() ? 0.0f : fontSize * 0.6f;
			const glm::vec2 pageSize = glm::floor(glm::vec2(pictureSize * 0.46f, pictureSize * 0.58f));
			const glm::vec2 pageMin = glm::floor(centre - pageSize * 0.5f - glm::vec2(0.0f, lift));
			const PainterRect page{ pageMin, pageMin + pageSize };
			const float fold = std::floor(pageSize.x * 0.32f);
			painter.AddRoundedRect(page, pageSize.x * 0.1f, PainterStyle::Filled(Shade(theme.FileIcon, look, theme)));
			const glm::vec4 folded{ theme.FileIcon.r * 0.65f, theme.FileIcon.g * 0.65f, theme.FileIcon.b * 0.65f, theme.FileIcon.a };
			painter.AddTriangle({ page.Max.x - fold, page.Min.y }, { page.Max.x, page.Min.y + fold }, { page.Max.x - fold, page.Min.y + fold }, PainterStyle::Filled(Shade(folded, look, theme)));
			if (!card.TypeLabel.empty())
			{
				std::string cut;
				const std::string_view type = FitText(card.TypeLabel, pictureSize, cut);
				PaintText(painter, { std::floor(centre.x - CalcTextSize(type).x * 0.5f), page.Max.y + fontSize * 0.3f }, Shade(theme.TextMuted, look, theme), type);
			}
		}

		// The name, centred under the picture, cut to fit
		std::string cut;
		const std::string_view name = FitText(label, pictureSize, cut);
		PaintText(painter, { std::floor(centre.x - CalcTextSize(name).x * 0.5f), picture.Max.y + std::floor(padding * 0.5f) }, Shade(theme.Text, look, theme), name);
	}

	void PaintColorSwatch(WindowPainter& painter, const PainterRect& frame, const glm::vec3& color, const WidgetLook& look)
	{
		const Theme& theme = GetTheme();
		painter.AddRoundedRect(frame, theme.FrameRounding, Filled(Shade(glm::vec4(color, 1.0f), look, theme), ControlEffects(look, theme, false)));
		const glm::vec4 border = glm::mix(theme.SwatchBorder, theme.SwatchBorderHovered, std::max(look.Hovered, look.Pressed));
		painter.AddRoundedRect(frame, theme.FrameRounding, PainterStyle::Outlined(Shade(border, look, theme), theme.FrameBorderWidth));
	}

	glm::vec2 CalcButtonSize(const char* label)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const glm::vec2 labelSize = CalcTextSize(VisibleLabel(label));
		return glm::vec2(labelSize.x + style.FramePadding.x * 2.0f, ImGui::GetFrameHeight());
	}

	glm::vec2 CalcIconButtonSize(const char* label, EIcon icon, bool bDropdown, float height)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttonHeight = height > 0.0f ? height : ImGui::GetFrameHeight();
		const float labelWidth = CalcTextSize(VisibleLabel(label)).x;
		const float iconSize = icon != EIcon::None ? std::floor(buttonHeight * GetTheme().IconSize) : 0.0f;
		const float dropdownWidth = bDropdown ? DropdownWidth(buttonHeight) : 0.0f;
		// An icon alone gets a button somewhat wider than high
		if (labelWidth <= 0.0f)
		{
			return glm::vec2(std::floor(buttonHeight * 1.6f) + dropdownWidth, buttonHeight);
		}
		const float gap = iconSize > 0.0f ? style.ItemInnerSpacing.x : 0.0f;
		return glm::vec2(std::floor(style.FramePadding.x * 2.0f + iconSize + gap + labelWidth + dropdownWidth), buttonHeight);
	}

	glm::vec2 CalcBarTabSize(const char* label, float height)
	{
		const float tabHeight = height > 0.0f ? height : ImGui::GetFrameHeight();
		return glm::vec2(std::floor(CalcTextSize(VisibleLabel(label)).x + tabHeight * 1.4f), tabHeight);
	}

	glm::vec2 CalcDocumentTabSize(const char* label, float height)
	{
		const float tabHeight = height > 0.0f ? height : ImGui::GetFrameHeight();
		const DocumentTabLayout layout = GetDocumentTabLayout(tabHeight, GetTheme());
		return glm::vec2(std::floor(layout.Padding + layout.IconSize + layout.Gap + CalcTextSize(VisibleLabel(label)).x + layout.CloseWidth), tabHeight);
	}

	glm::vec2 CalcStatusItemSize(const char* text, EStatusIcon icon, float height)
	{
		const float iconWidth = icon != EStatusIcon::None ? std::floor(height * StatusIconFraction) + std::floor(height * StatusIconGap) : 0.0f;
		return glm::vec2(std::floor(iconWidth + CalcTextSize(text).x), height);
	}

	glm::vec2 CalcTitlebarLogoSize(const char* name, float height)
	{
		return glm::vec2(std::floor(height * LogoFraction) + std::floor(height * LogoGap) + std::floor(CalcTextSize(name).x), height);
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

	float CalcTreeRowHeight()
	{
		return ImGui::GetFrameHeight();
	}

	float CalcAxisTagWidth()
	{
		return std::floor(CalcTextSize("W").x + ImGui::GetStyle().FramePadding.x * 2.0f);
	}

	float CalcPropertyValueX(float rowMinX, float rowMaxX)
	{
		return std::floor(rowMinX + (rowMaxX - rowMinX) * GetTheme().PropertyLabelWidth);
	}

	float CalcSearchTextOffset()
	{
		const float frameHeight = ImGui::GetFrameHeight();
		return SearchIconLeft(frameHeight) + SearchIconSize(frameHeight, GetTheme()) + ImGui::GetStyle().ItemInnerSpacing.x;
	}

	glm::vec2 CalcAssetCardSize()
	{
		// The picture is square; under it half the padding, the name, and the padding
		const Theme& theme = GetTheme();
		const float pictureSize = theme.AssetCardWidth - theme.AssetCardPadding * 2.0f;
		return glm::vec2(theme.AssetCardWidth, std::floor(theme.AssetCardPadding * 1.5f + pictureSize + ImGui::GetFontSize() + theme.AssetCardPadding));
	}

	glm::vec2 CalcCheckboxSize(const char* label)
	{
		const float labelWidth = CalcTextSize(VisibleLabel(label)).x;
		const float frameHeight = ImGui::GetFrameHeight();
		const float boxSize = std::floor(frameHeight * GetTheme().CheckboxSize);
		return glm::vec2(boxSize + (labelWidth > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + labelWidth : 0.0f), frameHeight);
	}
}

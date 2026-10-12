// The painted window chrome (UIChrome.h) in a headless ImGui context with a real dock space:
// the style it takes over, the cards of docked and floating windows, where their segments go in
// the draw lists, and the splitter between docked cards. No GPU is involved.
#include "PainterCommands.h"
#include "UIChrome.h"
#include "UITheme.h"
#include "WindowPainter.h"

#include <imgui.h>
#include <imgui_internal.h> // dock nodes and the DockBuilder

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Engine;
	using Nyx::UI::EWindowPainterPlacement;
	using Nyx::UI::WindowPainter;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) < 1e-3f;
	}

	bool Near(const PainterRect& a, const PainterRect& b)
	{
		return Near(a.Min.x, b.Min.x) && Near(a.Min.y, b.Min.y) && Near(a.Max.x, b.Max.x) && Near(a.Max.y, b.Max.y);
	}

	PainterRect RectAt(ImVec2 position, ImVec2 size)
	{
		return PainterRect{ { position.x, position.y }, { position.x + size.x, position.y + size.y } };
	}

	// An 800x600 display with a dock space filling it: "Left" and "Right" docked side by side,
	// with "Left 2" as a second tab beside "Left"; "Floating" over them, with a tab bar of "One"
	// and "Two"; and "Overlay", which asks for no background.
	class DockedEditor
	{
	public:
		DockedEditor()
		{
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			// No imgui.ini next to wherever the tests run
			io.IniFilename = nullptr;
			io.Fonts->AddFontDefault();
		}

		~DockedEditor()
		{
			Nyx::UI::SetPaintedChrome(false);
			ImGui::DestroyContext(Context);
		}

		DockedEditor(const DockedEditor&) = delete;
		DockedEditor& operator=(const DockedEditor&) = delete;

		// Builds one frame and ends it, as the renderer does before painting the cards
		void BuildFrame()
		{
			ImGui::NewFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
			const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
			ImGui::Begin("Dock space host", nullptr, hostFlags);
			const ImGuiID dockSpace = ImGui::GetID("Dock space");
			if (ImGui::GetFrameCount() == 1)
			{
				// Left and right halves, once
				ImGui::DockBuilderRemoveNode(dockSpace);
				ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockSpace, ImVec2(780.0f, 580.0f));
				ImGui::DockBuilderSplitNode(dockSpace, ImGuiDir_Left, 0.5f, &LeftNode, &RightNode);
				ImGui::DockBuilderDockWindow("Left", LeftNode);
				ImGui::DockBuilderDockWindow("Left 2", LeftNode);
				ImGui::DockBuilderDockWindow("Right", RightNode);
				ImGui::DockBuilderFinish(dockSpace);
			}
			ImGui::DockSpace(dockSpace);
			ImGui::End();

			for (const char* name : { "Left", "Left 2", "Right" })
			{
				ImGui::Begin(name);
				ImGui::TextUnformatted(name);
				ImGui::End();
			}

			ImGui::SetNextWindowPos(ImVec2(300.0f, 200.0f));
			ImGui::SetNextWindowSize(ImVec2(200.0f, 120.0f));
			ImGui::Begin("Floating", nullptr, ImGuiWindowFlags_NoDocking);
			if (ImGui::BeginTabBar("Tabs"))
			{
				for (const char* name : { "One", "Two" })
				{
					const bool bSelect = SelectTab != nullptr && std::strcmp(SelectTab, name) == 0;
					if (ImGui::BeginTabItem(name, nullptr, bSelect ? ImGuiTabItemFlags_SetSelected : 0))
					{
						ImGui::TextUnformatted(name);
						ImGui::EndTabItem();
					}
				}
				ImGui::EndTabBar();
			}
			SelectTab = nullptr;
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(40.0f, 400.0f));
			ImGui::SetNextWindowSize(ImVec2(100.0f, 60.0f));
			ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
			ImGui::End();

			ImGui::EndFrame();
		}

		// Paints the cards of the frame just built, gathers the painters and renders the frame,
		// so the dock host's channels are merged as the renderer would see them
		std::vector<WindowPainter*> PaintCards()
		{
			Nyx::UI::PaintWindowCards();
			const std::span<WindowPainter* const> painters = Nyx::UI::EndWindowPainters();
			std::vector<WindowPainter*> result(painters.begin(), painters.end());
			ImGui::Render();
			return result;
		}

		ImGuiDockNode& GetLeft() const { return *ImGui::DockBuilderGetNode(LeftNode); }
		ImGuiDockNode& GetRight() const { return *ImGui::DockBuilderGetNode(RightNode); }

		// The floating window's tab to select in the next frame built, by name
		const char* SelectTab = nullptr;

	private:
		ImGuiContext* Context = nullptr;
		ImGuiID LeftNode = 0;
		ImGuiID RightNode = 0;
	};

	ImGuiWindow& FindWindow(const char* name)
	{
		ImGuiWindow* window = ImGui::FindWindowByName(name);
		Require(window != nullptr, std::string("No window named ") + name);
		return *window;
	}

	WindowPainter* FindPainter(const std::vector<WindowPainter*>& painters, const ImDrawList* drawList, EWindowPainterPlacement placement)
	{
		const auto found = std::find_if(painters.begin(), painters.end(), [&](const WindowPainter* painter)
			{
				return painter->GetDrawList() == drawList && painter->GetPlacement() == placement;
			});
		return found != painters.end() ? *found : nullptr;
	}

	// The commands of a type a painter recorded
	std::vector<PainterCommand> CommandsOfType(const WindowPainter& painter, EPainterCommandType type)
	{
		std::vector<PainterCommand> result;
		for (const PainterCommand& command : painter.GetPainter().GetCommands())
		{
			if (command.Type == static_cast<uint32_t>(type))
			{
				result.push_back(command);
			}
		}
		return result;
	}

	// The upright lines a painter recorded: splitters between cards side by side
	std::vector<PainterLinePayload> UprightLines(const WindowPainter& painter)
	{
		std::vector<PainterLinePayload> result;
		for (const PainterCommand& command : CommandsOfType(painter, EPainterCommandType::Line))
		{
			const auto line = GetPayload<PainterLinePayload>(command);
			if (Near(line.From.x, line.To.x))
			{
				result.push_back(line);
			}
		}
		return result;
	}

	// The level lines a painter recorded: tab underlines, with their colours
	std::vector<PainterCommand> LevelLines(const WindowPainter& painter)
	{
		std::vector<PainterCommand> result;
		for (const PainterCommand& command : CommandsOfType(painter, EPainterCommandType::Line))
		{
			const auto line = GetPayload<PainterLinePayload>(command);
			if (Near(line.From.y, line.To.y))
			{
				result.push_back(command);
			}
		}
		return result;
	}

	// Where ImGui put a tab, worked out as TabItemEx() does for tabs in the bar's scrolling part
	PainterRect TabRect(const ImGuiTabBar& tabBar, const ImGuiTabItem& tab)
	{
		const float x = tabBar.BarRect.Min.x + ImTrunc(tab.Offset - tabBar.ScrollingAnim);
		return PainterRect{ { x, tabBar.BarRect.Min.y }, { x + tab.Width, tabBar.BarRect.Max.y } };
	}

	// The pill the chrome paints behind a tab
	PainterRect PillOf(const PainterRect& tab)
	{
		const float inset = Nyx::UI::GetTheme().TabPillInset;
		return PainterRect{ { tab.Min.x, tab.Min.y + inset }, { tab.Max.x, tab.Max.y - inset } };
	}

	bool HasRect(const WindowPainter& painter, const PainterRect& rect)
	{
		const std::vector<PainterCommand> rects = CommandsOfType(painter, EPainterCommandType::Rect);
		return std::any_of(rects.begin(), rects.end(), [&](const PainterCommand& command) { return Near(GetPayload<PainterRectPayload>(command).Rect, rect); });
	}

	const ImGuiTabItem& FindTab(const ImGuiTabBar& tabBar, ImGuiID id)
	{
		const auto found = std::find_if(tabBar.Tabs.begin(), tabBar.Tabs.end(), [&](const ImGuiTabItem& tab) { return tab.ID == id; });
		Require(found != tabBar.Tabs.end(), "No tab with that ID");
		return *found;
	}

	// Where a window painter's first segment callback is in a draw list, and how many indices
	// ImGui drew before it; -1 if it has none there
	int FindFirstCallback(const ImDrawList& drawList, const WindowPainter& painter, unsigned int& outIndicesBefore)
	{
		outIndicesBefore = 0;
		for (int index = 0; index < drawList.CmdBuffer.Size; ++index)
		{
			const ImDrawCmd& command = drawList.CmdBuffer[index];
			if (command.UserCallback == &WindowPainter::DrawSegmentCallback && command.UserCallbackData != nullptr)
			{
				WindowPainter::SegmentCallbackData data;
				std::memcpy(&data, command.UserCallbackData, sizeof(data));
				if (data.Painter == &painter)
				{
					return index;
				}
			}
			outIndicesBefore += command.ElemCount;
		}
		return -1;
	}

	void TestTheStyleIsTakenOverAndGivenBack()
	{
		const ImGuiStyle before = ImGui::GetStyle();
		Nyx::UI::SetPaintedChrome(true);
		const ImGuiStyle& style = ImGui::GetStyle();
		Require(Nyx::UI::IsPaintedChromeEnabled(), "The chrome should be on");
		Require(style.Colors[ImGuiCol_WindowBg].w == 0.0f && style.Colors[ImGuiCol_PopupBg].w == 0.0f && style.Colors[ImGuiCol_TitleBgActive].w == 0.0f,
			"ImGui's window, popup and title bar backgrounds should be transparent while the chrome paints them");
		Require(style.Colors[ImGuiCol_Tab].w == 0.0f && style.Colors[ImGuiCol_TabSelected].w == 0.0f && style.Colors[ImGuiCol_TabHovered].w == 0.0f,
			"ImGui's tab shapes should be transparent while the chrome paints them");
		Require(style.WindowBorderSize == 0.0f && style.PopupBorderSize == 0.0f, "Windows and popups should have no borders");
		Require(style.DockingSeparatorSize == Nyx::UI::GetTheme().CardGap, "ImGui should leave the theme's card gap between dock nodes");

		Nyx::UI::SetPaintedChrome(false);
		Require(!Nyx::UI::IsPaintedChromeEnabled(), "The chrome should be off");
		Require(std::memcmp(style.Colors, before.Colors, sizeof(style.Colors)) == 0 && style.WindowBorderSize == before.WindowBorderSize &&
			style.PopupBorderSize == before.PopupBorderSize && style.DockingSeparatorSize == before.DockingSeparatorSize,
			"Turning the chrome off should give ImGui's style back as it was");
	}

	void TestNothingIsPaintedWhileTheChromeIsOff(DockedEditor& editor)
	{
		editor.BuildFrame();
		const std::vector<WindowPainter*> painters = editor.PaintCards();
		Require(painters.empty(), "With the chrome off, no cards should be painted");
	}

	void TestDockedWindowsAreCardsWithAGapBetween(DockedEditor& editor)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		// One frame for ImGui to lay the nodes out with the new gap
		editor.BuildFrame();
		editor.PaintCards();

		editor.BuildFrame();
		ImGuiDockNode& left = editor.GetLeft();
		ImGuiDockNode& right = editor.GetRight();
		ImGuiWindow& host = *left.HostWindow;
		const int channelBefore = host.DrawList->_Splitter._Current;
		const std::vector<WindowPainter*> painters = editor.PaintCards();

		Require(Near(right.Pos.x - (left.Pos.x + left.Size.x), theme.CardGap), "ImGui should leave the card gap between the docked halves");

		const WindowPainter* cards = FindPainter(painters, host.DrawList, EWindowPainterPlacement::InOrder);
		Require(cards != nullptr, "The dock host should get a painter for the cards");
		const std::vector<PainterCommand> rects = CommandsOfType(*cards, EPainterCommandType::Rect);
		const auto isCardOf = [&](const PainterCommand& command, const ImGuiDockNode& node)
		{
			return command.OutlineWidth == 0.0f && Near(GetPayload<PainterRectPayload>(command).Rect, RectAt(node.Pos, node.Size));
		};
		const auto leftCard = std::find_if(rects.begin(), rects.end(), [&](const PainterCommand& command) { return isCardOf(command, left); });
		const auto rightCard = std::find_if(rects.begin(), rects.end(), [&](const PainterCommand& command) { return isCardOf(command, right); });
		Require(leftCard != rects.end() && rightCard != rects.end(), "Each docked node should be a card exactly its size");
		Require(leftCard->Color == PackPremultipliedColor(theme.Card) && GetPayload<PainterRectPayload>(*leftCard).Radii.TopLeft == theme.CardRounding,
			"A docked card should have the theme's colour and rounding");
		Require(leftCard->Group != PainterNoGroup && leftCard->Group == rightCard->Group && cards->GetPainter().GetGroups()[leftCard->Group].ShadowColor != 0,
			"The docked cards should cast one shadow together");
		Require(host.DrawList->_Splitter._Count <= 1 || channelBefore == host.DrawList->_Splitter._Current, "The host's channel should be as it was");

		// Rendered, the host's channels are merged: the cards come before everything else in it,
		// and its tab bars after them.
		unsigned int indicesBefore = 0;
		const int callback = FindFirstCallback(*host.DrawList, *cards, indicesBefore);
		Require(callback >= 0 && indicesBefore == 0, "The cards should be drawn before anything else of the dock host");
		Require(static_cast<unsigned int>(host.DrawList->IdxBuffer.Size) > indicesBefore, "The dock host's tab bars should be drawn after the cards");

		// The docked windows get no card of their own.
		Require(FindPainter(painters, FindWindow("Left").DrawList, EWindowPainterPlacement::Behind) == nullptr, "A docked window is its node's card");
	}

	void TestFloatingWindowsGetACardBehindThem(DockedEditor& editor)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		editor.BuildFrame();
		const std::vector<WindowPainter*> painters = editor.PaintCards();

		ImGuiWindow& floating = FindWindow("Floating");
		const WindowPainter* card = FindPainter(painters, floating.DrawList, EWindowPainterPlacement::Behind);
		Require(card != nullptr, "A floating window should get a card behind it");
		const std::vector<PainterCommand> rects = CommandsOfType(*card, EPainterCommandType::Rect);
		Require(!rects.empty() && Near(GetPayload<PainterRectPayload>(rects[0]).Rect, RectAt(floating.Pos, floating.Size)), "The floating card should be the window's rect");
		Require(rects[0].Color == PackPremultipliedColor(theme.FloatingCard) && rects[0].Group != PainterNoGroup &&
			card->GetPainter().GetGroups()[rects[0].Group].ShadowColor != 0,
			"A floating card should have the theme's floating colour and a shadow");

		unsigned int indicesBefore = 0;
		Require(FindFirstCallback(*floating.DrawList, *card, indicesBefore) == 0, "The floating card should be the first thing in the window's draw list");

		Require(FindPainter(painters, FindWindow("Overlay").DrawList, EWindowPainterPlacement::Behind) == nullptr, "A window without a background should get no card");
		Require(FindPainter(painters, FindWindow("Dock space host").DrawList, EWindowPainterPlacement::Behind) == nullptr,
			"The dock space's host window, without a background, should get no card");
	}

	void TestASplitterShowsWhileHovered(DockedEditor& editor)
	{
		// Not hovered: no line
		editor.BuildFrame();
		ImGuiDockNode& left = editor.GetLeft();
		std::vector<WindowPainter*> painters = editor.PaintCards();
		const WindowPainter* cards = FindPainter(painters, left.HostWindow->DrawList, EWindowPainterPlacement::InOrder);
		Require(cards != nullptr && UprightLines(*cards).empty(), "A splitter nobody hovers should not show");

		// The mouse in the gap, below the floating window, for a few frames: past ImGui's hover
		// delay, and the line fades in
		const float gapX = left.Pos.x + left.Size.x + 0.5f * Nyx::UI::GetTheme().CardGap;
		const float middleY = left.Pos.y + 0.8f * left.Size.y;
		ImGui::GetIO().AddMousePosEvent(gapX, middleY);
		for (int frame = 0; frame < 6; ++frame)
		{
			editor.BuildFrame();
			painters = editor.PaintCards();
		}

		cards = FindPainter(painters, left.HostWindow->DrawList, EWindowPainterPlacement::InOrder);
		const std::vector<PainterLinePayload> lines = cards != nullptr ? UprightLines(*cards) : std::vector<PainterLinePayload>{};
		Require(lines.size() == 1, "A hovered splitter should show as one line; ImGui may have changed how it makes the splitter's ID");
		const PainterLinePayload& line = lines[0];
		Require(Near(line.From.x, gapX) && Near(line.To.x, gapX) && line.From.y < middleY && line.To.y > middleY, "The line should run upright along the middle of the gap");

		ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}

	// Frames enough for every eased value to arrive
	void Settle(DockedEditor& editor)
	{
		for (int frame = 0; frame < 60; ++frame)
		{
			editor.BuildFrame();
			editor.PaintCards();
		}
	}

	const ImGuiTabBar& FindTabBarIn(const ImGuiWindow& window)
	{
		ImGuiContext& g = *GImGui;
		for (int index = 0; index < g.TabBars.GetMapSize(); ++index)
		{
			if (const ImGuiTabBar* tabBar = g.TabBars.TryGetMapData(index); tabBar != nullptr && tabBar->Window == &window)
			{
				return *tabBar;
			}
		}
		throw std::runtime_error(std::string("No tab bar in ") + window.Name);
	}

	// The level line under a tab: its ends and colour
	struct Underline
	{
		float Left = 0.0f;
		float Right = 0.0f;
		float Y = 0.0f;
		uint32_t Color = 0;
	};

	// The underline under `tabBar`; a painter may paint several tab bars
	Underline FindUnderline(const WindowPainter& painter, const ImGuiTabBar& tabBar)
	{
		std::vector<PainterCommand> lines = LevelLines(painter);
		const ImRect& bar = tabBar.BarRect;
		std::erase_if(lines, [&](const PainterCommand& command)
			{
				const auto line = GetPayload<PainterLinePayload>(command);
				return line.From.y < bar.Min.y || line.From.y > bar.Max.y || line.From.x < bar.Min.x || line.To.x > bar.Max.x;
			});
		Require(lines.size() == 1, "A tab bar should have one underline, got " + std::to_string(lines.size()));
		const auto line = GetPayload<PainterLinePayload>(lines[0]);
		return Underline{ line.From.x, line.To.x, line.From.y, lines[0].Color };
	}

	void TestDockedTabBarsArePainted(DockedEditor& editor)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		Settle(editor);
		editor.BuildFrame();
		const ImGuiDockNode& left = editor.GetLeft();
		const std::vector<WindowPainter*> painters = editor.PaintCards();
		Require(left.TabBar != nullptr && left.TabBar->Tabs.Size == 2, "Left and Left 2 should share a tab bar");
		const ImGuiTabBar& tabBar = *left.TabBar;
		const WindowPainter* cards = FindPainter(painters, left.HostWindow->DrawList, EWindowPainterPlacement::InOrder);
		Require(cards != nullptr, "The dock host should get a painter");

		const ImGuiTabItem& shown = FindTab(tabBar, tabBar.VisibleTabId);
		const ImGuiTabItem& other = tabBar.Tabs[0].ID == shown.ID ? tabBar.Tabs[1] : tabBar.Tabs[0];
		const PainterRect shownRect = TabRect(tabBar, shown);
		Require(HasRect(*cards, PillOf(shownRect)), "The tab that shows should have a pill behind it");
		Require(!HasRect(*cards, PillOf(TabRect(tabBar, other))), "A tab nobody hovers or selected should have none");

		const Underline underline = FindUnderline(*cards, tabBar);
		Require(Near(underline.Left, shownRect.Min.x + tabBar.FramePadding.x) && Near(underline.Right, shownRect.Max.x - tabBar.FramePadding.x) &&
			Near(underline.Y, tabBar.BarRect.Max.y - 0.5f * theme.TabUnderlineWidth),
			"The underline should run under the tab that shows, inside its padding, at the bar's bottom");
		const bool bFocused = (tabBar.Flags & ImGuiTabBarFlags_IsFocused) != 0;
		Require(underline.Color == PackPremultipliedColor(bFocused ? theme.TabUnderline : theme.TabUnderlineDimmed),
			"The underline should be the accent where the tab bar has focus, and dimmed where it hasn't");
		Require(HasRect(*cards, PainterRect{ { tabBar.SeparatorMinX, tabBar.BarRect.Max.y - 1.0f }, { tabBar.SeparatorMaxX, tabBar.BarRect.Max.y } }),
			"A line should run under the whole tab bar");

		// Hovered for a few frames, the other tab gets a pill too.
		const PainterRect otherRect = TabRect(tabBar, other);
		ImGui::GetIO().AddMousePosEvent(0.5f * (otherRect.Min.x + otherRect.Max.x), 0.5f * (otherRect.Min.y + otherRect.Max.y));
		std::vector<WindowPainter*> hovered;
		for (int frame = 0; frame < 6; ++frame)
		{
			editor.BuildFrame();
			hovered = editor.PaintCards();
		}
		const WindowPainter* hoveredCards = FindPainter(hovered, left.HostWindow->DrawList, EWindowPainterPlacement::InOrder);
		Require(hoveredCards != nullptr && HasRect(*hoveredCards, PillOf(otherRect)), "A hovered tab should get a pill");
		ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}

	void TestTabBarsInWindowsArePaintedBehindThem(DockedEditor& editor)
	{
		Settle(editor);
		editor.BuildFrame();
		const std::vector<WindowPainter*> painters = editor.PaintCards();
		ImGuiWindow& floating = FindWindow("Floating");
		const ImGuiTabBar& tabBar = FindTabBarIn(floating);
		const WindowPainter* behind = FindPainter(painters, floating.DrawList, EWindowPainterPlacement::Behind);
		Require(behind != nullptr, "The floating window should get a painter behind it");

		// The card first, then the tab bar on it, both before anything ImGui drew in the window
		const std::vector<PainterCommand>& commands = behind->GetPainter().GetCommands();
		const PainterRect pill = PillOf(TabRect(tabBar, FindTab(tabBar, tabBar.VisibleTabId)));
		const auto indexOf = [&](const PainterRect& rect)
		{
			for (size_t index = 0; index < commands.size(); ++index)
			{
				if (commands[index].Type == static_cast<uint32_t>(EPainterCommandType::Rect) && Near(GetPayload<PainterRectPayload>(commands[index]).Rect, rect))
				{
					return static_cast<int>(index);
				}
			}
			return -1;
		};
		const int cardIndex = indexOf(RectAt(floating.Pos, floating.Size));
		const int pillIndex = indexOf(pill);
		Require(cardIndex >= 0 && pillIndex > cardIndex, "A tab bar in a floating window should be painted on its card");
		Require(FindUnderline(*behind, tabBar).Color == PackPremultipliedColor(Nyx::UI::GetTheme().TabUnderline), "A tab bar in a window counts as focused, as ImGui has it");
	}

	void TestTheUnderlineSlidesToTheSelectedTab(DockedEditor& editor)
	{
		Settle(editor);
		ImGuiWindow& floating = FindWindow("Floating");
		const ImGuiTabBar& tabBar = FindTabBarIn(floating);
		Require(tabBar.Tabs.Size == 2 && tabBar.VisibleTabId == tabBar.Tabs[0].ID, "The floating window's first tab should show first");
		const ImGuiID secondTab = tabBar.Tabs[1].ID;
		const float from = TabRect(tabBar, tabBar.Tabs[0]).Min.x + tabBar.FramePadding.x;
		const float to = TabRect(tabBar, tabBar.Tabs[1]).Min.x + tabBar.FramePadding.x;

		// ImGui switches tabs a frame after it is asked to; the frame it does, the underline has
		// moved part of the way.
		editor.SelectTab = "Two";
		bool bSwitched = false;
		for (int frame = 0; frame < 4 && !bSwitched; ++frame)
		{
			editor.BuildFrame();
			const std::vector<WindowPainter*> painters = editor.PaintCards();
			if (tabBar.VisibleTabId == secondTab)
			{
				bSwitched = true;
				const float left = FindUnderline(*FindPainter(painters, floating.DrawList, EWindowPainterPlacement::Behind), tabBar).Left;
				Require(left > from + 0.5f && left < to - 0.5f, "The underline should slide towards the newly selected tab, not jump");
			}
		}
		Require(bSwitched, "Selecting the second tab should show it");

		Settle(editor);
		editor.BuildFrame();
		const std::vector<WindowPainter*> painters = editor.PaintCards();
		Require(Near(FindUnderline(*FindPainter(painters, floating.DrawList, EWindowPainterPlacement::Behind), tabBar).Left, to),
			"In the end the underline should be under the selected tab");
	}
}

int main()
{
	try
	{
		DockedEditor editor;
		TestTheStyleIsTakenOverAndGivenBack();
		TestNothingIsPaintedWhileTheChromeIsOff(editor);

		Nyx::UI::SetPaintedChrome(true);
		TestDockedWindowsAreCardsWithAGapBetween(editor);
		TestFloatingWindowsGetACardBehindThem(editor);
		TestDockedTabBarsArePainted(editor);
		TestTabBarsInWindowsArePaintedBehindThem(editor);
		TestTheUnderlineSlidesToTheSelectedTab(editor);
		TestASplitterShowsWhileHovered(editor);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All UI chrome tests passed\n";
	return 0;
}

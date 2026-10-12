#include "UIChrome.h"

#include "UIAnimation.h"
#include "UITheme.h"
#include "WindowPainter.h"

#include <imgui_internal.h> // ImGui's windows, dock nodes, and how it makes IDs

#include <algorithm>
#include <iterator>
#include <vector>

namespace Nyx::UI
{
	using Engine::PainterRect;
	using Engine::PainterStyle;

	namespace
	{
		// ImGui's colours the chrome paints instead; their alpha is 0 while it is on
		constexpr ImGuiCol PaintedColors[] = {
			ImGuiCol_WindowBg,
			ImGuiCol_PopupBg,
			ImGuiCol_Border,
			ImGuiCol_TitleBg,
			ImGuiCol_TitleBgActive,
			ImGuiCol_TitleBgCollapsed,
			ImGuiCol_DockingEmptyBg,
			// Floating windows' resize grips, and ImGui's own highlight of a hovered splitter,
			// which takes these colours
			ImGuiCol_ResizeGrip,
			ImGuiCol_ResizeGripHovered,
			ImGuiCol_ResizeGripActive,
			// Tabs: their shapes, overlines and the line under a tab bar, which takes the selected colour
			ImGuiCol_Tab,
			ImGuiCol_TabHovered,
			ImGuiCol_TabSelected,
			ImGuiCol_TabSelectedOverline,
			ImGuiCol_TabDimmed,
			ImGuiCol_TabDimmedSelected,
			ImGuiCol_TabDimmedSelectedOverline,
			// Scrollbars: their track and grab
			ImGuiCol_ScrollbarBg,
			ImGuiCol_ScrollbarGrab,
			ImGuiCol_ScrollbarGrabHovered,
			ImGuiCol_ScrollbarGrabActive,
		};

		// What the chrome changes in ImGui's style, as it was before, to put back when it goes off
		struct StyleBeforeChrome
		{
			float ColorAlphas[std::size(PaintedColors)] = {};
			float WindowBorderSize = 0.0f;
			float PopupBorderSize = 0.0f;
			float DockingSeparatorSize = 0.0f;
		};

		// A glow a window asked for this frame (AddWindowGlow())
		struct WindowGlow
		{
			const ImGuiWindow* Window = nullptr;
			ImTextureRef Texture;
			PainterRect Rect;
			float Reach = 0.0f;
			float Strength = 0.0f;
		};

		struct ChromeState
		{
			bool bEnabled = false;
			StyleBeforeChrome StyleBefore;

			// This frame's window glows, and the frame and context they belong to
			std::vector<WindowGlow> Glows;
			const ImGuiContext* GlowsContext = nullptr;
			int GlowsFrame = -1;

			// What glass shows, from the renderer, and the draw lists of this frame's glass windows
			ImTextureID GlassBackdrop = ImTextureID_Invalid;
			// What glass shows in OS windows of their own: one colour (SetPlainGlassBackdrop())
			ImTextureID PlainGlassBackdrop = ImTextureID_Invalid;
			std::vector<const ImDrawList*> GlassDrawLists;
		};

		ChromeState& GetState()
		{
			static ChromeState state;
			return state;
		}

		// The window glows asked for this frame; those of an earlier frame are forgotten.
		std::vector<WindowGlow>& GetGlows()
		{
			ChromeState& state = GetState();
			if (state.GlowsContext != ImGui::GetCurrentContext() || state.GlowsFrame != ImGui::GetFrameCount())
			{
				state.Glows.clear();
				state.GlowsContext = ImGui::GetCurrentContext();
				state.GlowsFrame = ImGui::GetFrameCount();
			}
			return state.Glows;
		}

		// The glows `window` asked for this frame, painted before its card
		void PaintGlowsOf(WindowPainter& painter, const ImGuiWindow& window)
		{
			for (const WindowGlow& glow : GetGlows())
			{
				if (glow.Window == &window)
				{
					painter.AddImageGlow(glow.Texture, glow.Rect, glow.Reach, glow.Strength);
				}
			}
		}

		// ImGui waits this long before it shows a hovered splitter (WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER
		// in imgui.cpp), so that the mouse passing over one doesn't light it up
		constexpr float SplitterHoverDelay = 0.04f;

		// A host draws into two channels until ImGui renders it (DOCKING_HOST_DRAW_CHANNEL_BG and
		// _FG in imgui.cpp): its docked windows' backgrounds into this one, its tab bars into the
		// next, which comes after it
		constexpr int DockHostBackgroundChannel = 0;

		// The rect at `position` of `size`, as ImGui gives windows, dock nodes and viewports
		PainterRect RectAt(ImVec2 position, ImVec2 size)
		{
			return PainterRect{ { position.x, position.y }, { position.x + size.x, position.y + size.y } };
		}

		ImVec2 ToImVec2(glm::vec2 point)
		{
			return ImVec2(point.x, point.y);
		}

		// Whether the window fills an OS window of its own (ImGui's multi-viewports)
		bool OwnsOsWindow(const ImGuiWindow& window)
		{
			return window.Viewport != nullptr && window.Viewport->Window == &window;
		}

		// The ID ImGui gives a split dock node's splitter (DockNodeTreeUpdateSplitter()): "##Splitter",
		// under the node's ID, in the host window's ID scope. It is worked out the way ImGui
		// hashes IDs, so it has to follow ImGui; NyxUIChromeTests checks it by hovering a splitter.
		ImGuiID GetSplitterId(const ImGuiDockNode& node)
		{
			const int nodeId = static_cast<int>(node.ID);
			const ImGuiID nodeScope = ImHashData(&nodeId, sizeof(nodeId), node.HostWindow->ID);
			return ImHashStr("##Splitter", 0, nodeScope);
		}

		// The hairline just inside a card's edge
		void PaintCardBorder(WindowPainter& painter, const PainterRect& card, float rounding)
		{
			const Theme& theme = GetTheme();
			if (theme.CardBorder.a <= 0.0f)
			{
				return;
			}
			// An outline is centred on the rect's edge; half a pixel in keeps it inside the card.
			painter.AddRoundedRect(Engine::Grow(card, -0.5f), std::max(rounding - 0.5f, 0.0f), PainterStyle::Outlined(theme.CardBorder, 1.0f));
		}

		// A thin line along the gap between a split node's two halves, while its splitter is
		// hovered or dragged; it fades in and out.
		void PaintSplitter(WindowPainter& painter, const ImGuiDockNode& node)
		{
			const ImGuiContext& g = *GImGui;
			const Theme& theme = GetTheme();
			const ImGuiDockNode& first = *node.ChildNodes[0];
			const ImGuiDockNode& second = *node.ChildNodes[1];
			if (!first.IsVisible || !second.IsVisible)
			{
				return;
			}

			const ImGuiID id = GetSplitterId(node);
			const bool bHovered = g.HoveredId == id && g.HoveredIdTimer >= SplitterHoverDelay;
			const float shown = Animate(id, bHovered || g.ActiveId == id ? 1.0f : 0.0f, theme.AnimationSpeed);
			if (shown <= 0.0f)
			{
				return;
			}

			// The gap between the halves, as ImGui's splitter covers it. Along the split axis it
			// runs from the end of the first half to the start of the second.
			const int axis = node.SplitAxis;
			glm::vec2 min{ first.Pos.x, first.Pos.y };
			glm::vec2 max{ second.Pos.x, second.Pos.y };
			min[axis] += first.Size[axis];
			max[axis ^ 1] += second.Size[axis ^ 1];

			// Along the middle of the gap, as long as the cards beside it less their rounding
			glm::vec2 from{ 0.5f * (min.x + max.x), 0.5f * (min.y + max.y) };
			glm::vec2 to = from;
			from[axis ^ 1] = min[axis ^ 1] + theme.CardRounding;
			to[axis ^ 1] = max[axis ^ 1] - theme.CardRounding;

			glm::vec4 color = theme.Splitter;
			color.a *= shown;
			painter.AddLine(from, to, theme.SplitterWidth, Engine::EPainterLineCap::Round, PainterStyle::Filled(color));
		}

		// Where ImGui puts a tab (TabItemEx()): along the bar by its offset, scrolled with the bar
		// unless it sits in the bar's leading or trailing section
		PainterRect GetTabRect(const ImGuiTabBar& tabBar, const ImGuiTabItem& tab)
		{
			const bool bScrolls = (tab.Flags & ImGuiTabItemFlags_SectionMask_) == 0;
			const float x = tabBar.BarRect.Min.x + (bScrolls ? ImTrunc(tab.Offset - tabBar.ScrollingAnim) : tab.Offset);
			return PainterRect{ { x, tabBar.BarRect.Min.y }, { x + tab.Width, tabBar.BarRect.Max.y } };
		}

		// A pill `shown` of the way faded in
		void PaintPill(WindowPainter& painter, const PainterRect& pill, const glm::vec4& color, float shown)
		{
			if (shown <= 0.0f || color.a <= 0.0f)
			{
				return;
			}
			glm::vec4 faded = color;
			faded.a *= shown;
			painter.AddRoundedRect(pill, 0.5f * pill.GetHeight(), PainterStyle::Filled(faded));
		}

		// A tab bar's look, painted behind ImGui's tab labels: a line under the bar, a pill behind
		// the selected tab and behind a hovered one, and an underline under the selected tab, which
		// slides when another one is selected. `key` names the tab bar for its animations.
		void PaintTabBar(WindowPainter& painter, ImDrawList& drawList, const ImGuiTabBar& tabBar, ImGuiID key)
		{
			const ImGuiContext& g = *GImGui;
			const Theme& theme = GetTheme();
			const ImRect& bar = tabBar.BarRect;

			// Where ImGui would draw its line under the bar
			painter.AddRect(PainterRect{ { tabBar.SeparatorMinX, bar.Max.y - 1.0f }, { tabBar.SeparatorMaxX, bar.Max.y } }, PainterStyle::Filled(theme.Separator));

			// Tabs scrolled out of the bar are cut off where ImGui cuts them.
			drawList.PushClipRect(ImVec2(tabBar.ScrollingRectMinX, bar.Min.y), ImVec2(tabBar.ScrollingRectMaxX, bar.Max.y), true);
			const ImGuiTabItem* shownTab = nullptr;
			for (const ImGuiTabItem& tab : tabBar.Tabs)
			{
				// Tab buttons (ImGui::TabItemButton()) take ImGui's button colours.
				if (tab.LastFrameVisible != g.FrameCount || (tab.Flags & ImGuiTabItemFlags_Button) != 0)
				{
					continue;
				}
				const PainterRect rect = GetTabRect(tabBar, tab);
				const PainterRect pill{ { rect.Min.x, rect.Min.y + theme.TabPillInset }, { rect.Max.x, rect.Max.y - theme.TabPillInset } };

				// The tab whose contents show, as ImGui decides it; usually the selected one
				const bool bShown = tab.ID == tabBar.VisibleTabId;
				const bool bHovered = g.HoveredId == tab.ID || g.ActiveId == tab.ID;
				PaintPill(painter, pill, theme.TabSelected, Animate(ImHashStr("TabSelected", 0, tab.ID), bShown ? 1.0f : 0.0f, theme.AnimationSpeed));
				PaintPill(painter, pill, theme.TabHovered, Animate(ImHashStr("TabHovered", 0, tab.ID), bHovered ? 1.0f : 0.0f, theme.AnimationSpeed));
				if (bShown)
				{
					shownTab = &tab;
				}
			}

			if (shownTab != nullptr)
			{
				// The ends are eased relative to the bar, so the underline doesn't trail behind
				// when the window moves; it only slides along the bar.
				const PainterRect rect = GetTabRect(tabBar, *shownTab);
				const float left = Animate(ImHashStr("UnderlineLeft", 0, key), rect.Min.x + tabBar.FramePadding.x - bar.Min.x, theme.AnimationSpeed);
				const float right = Animate(ImHashStr("UnderlineRight", 0, key), rect.Max.x - tabBar.FramePadding.x - bar.Min.x, theme.AnimationSpeed);
				const float y = bar.Max.y - 0.5f * theme.TabUnderlineWidth;
				const bool bFocused = (tabBar.Flags & ImGuiTabBarFlags_IsFocused) != 0;
				painter.AddLine({ bar.Min.x + left, y }, { bar.Min.x + right, y }, theme.TabUnderlineWidth, Engine::EPainterLineCap::Round,
					PainterStyle::Filled(bFocused ? theme.TabUnderline : theme.TabUnderlineDimmed));
			}
			drawList.PopClipRect();
		}

		// Whether a window or dock host is glass: in the main window, which the backdrop is a
		// picture of, and not filling an OS window of its own
		bool IsGlass(const ImGuiWindow& window)
		{
			return window.Viewport == ImGui::GetMainViewport() && !OwnsOsWindow(window);
		}

		// A floating card's surface: glass where the renderer has a backdrop for it, opaque
		// otherwise.
		//  - In the main window, glass shows what lies behind it there. Its draw list is noted
		//    either way: the renderer draws the backdrop for it this frame, before the glass is
		//    drawn. The very first time, there is no backdrop yet when the cards are painted, so
		//    glass starts a frame later.
		//  - In an OS window of its own, nothing of the desktop can show (the window is opaque to
		//    the OS), so glass shows the plain backdrop, one colour: the same glass, tinted and lit
		//    along its edge, over an empty backdrop.
		void PaintFloatingSurface(WindowPainter& painter, const ImGuiWindow& window, const PainterRect& card)
		{
			ChromeState& state = GetState();
			const Theme& theme = GetTheme();
			const bool bGlass = IsGlass(window);
			if (bGlass && std::find(state.GlassDrawLists.begin(), state.GlassDrawLists.end(), window.DrawList) == state.GlassDrawLists.end())
			{
				state.GlassDrawLists.push_back(window.DrawList);
			}
			const ImTextureID backdrop = bGlass ? state.GlassBackdrop : state.PlainGlassBackdrop;
			if (backdrop != ImTextureID_Invalid)
			{
				painter.AddGlass(ImTextureRef(backdrop), card, theme.CardRounding, PainterStyle::Filled(theme.GlassTint), theme.Glass);
			}
			else
			{
				painter.AddRoundedRect(card, theme.CardRounding, PainterStyle::Filled(theme.FloatingCard));
			}
		}

		// ImGui draws the background and title bar of a window that fills an OS window of its own
		// opaque, whatever their colours' alpha: the OS window can't show what lies behind it. So
		// they would cover the card and tab bars painted behind them. They are the first things
		// ImGui draws into the window's draw list; their alpha is taken back to 0 here, as the
		// chrome makes their colours transparent everywhere else. The vertices are found by colour,
		// from the start of the draw list until one has another colour.
		void ClearOsWindowBackground(ImGuiWindow& window)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			std::vector<ImU32> colors;
			for (const ImGuiCol color : { ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed })
			{
				colors.push_back(ImGui::ColorConvertFloat4ToU32(style.Colors[color]) & ~IM_COL32_A_MASK);
			}
			for (ImDrawVert& vertex : window.DrawList->VtxBuffer)
			{
				const ImU32 color = vertex.col & ~IM_COL32_A_MASK;
				if (std::find(colors.begin(), colors.end(), color) == colors.end())
				{
					break;
				}
				vertex.col = color;
			}
		}

		// A dock host window and the dock nodes it shows this frame
		struct DockHost
		{
			ImGuiWindow* Window = nullptr;
			// Nodes with windows: each is a card
			std::vector<ImGuiDockNode*> Cards;
			// Split nodes: each has a splitter between its two halves
			std::vector<ImGuiDockNode*> Splits;
		};

		std::vector<DockHost> FindDockHosts()
		{
			const ImGuiContext& g = *GImGui;
			std::vector<DockHost> hosts;
			for (const ImGuiStoragePair& pair : g.DockContext.Nodes.Data)
			{
				ImGuiDockNode* node = static_cast<ImGuiDockNode*>(pair.val_p);
				if (node == nullptr || node->LastFrameActive != g.FrameCount || !node->IsVisible || node->HostWindow == nullptr ||
					!node->HostWindow->Active || node->HostWindow->Hidden)
				{
					continue;
				}
				const bool bCard = node->IsLeafNode() && node->Windows.Size > 0;
				if (!bCard && !node->IsSplitNode())
				{
					continue;
				}

				auto host = std::find_if(hosts.begin(), hosts.end(), [&](const DockHost& found) { return found.Window == node->HostWindow; });
				if (host == hosts.end())
				{
					host = hosts.insert(hosts.end(), DockHost{ node->HostWindow });
				}
				(bCard ? host->Cards : host->Splits).push_back(node);
			}
			return hosts;
		}

		void PaintDockHost(const DockHost& host)
		{
			if (host.Cards.empty() && host.Splits.empty())
			{
				return;
			}
			const Theme& theme = GetTheme();
			ImGuiWindow& window = *host.Window;
			ImDrawList& drawList = *window.DrawList;
			const ImGuiViewport& viewport = *window.Viewport;

			// The cards go into the host's background channel: behind its tab bars, which ImGui
			// draws into the next channel, and before the docked windows, which ImGui renders
			// after their host.
			const bool bTwoChannels = drawList._Splitter._Count > 1;
			const int channelBefore = drawList._Splitter._Current;
			if (bTwoChannels)
			{
				drawList.ChannelsSetCurrent(DockHostBackgroundChannel);
			}
			// Docked into the dock space, or floating together
			ImGuiDockNode* someNode = host.Cards.empty() ? host.Splits.front() : host.Cards.front();
			const bool bInDockSpace = ImGui::DockNodeGetRootNode(someNode)->IsDockSpace();

			// Shadows and glows reach past the cards. In the dock space, as far as its margin of
			// CardGap and no further: the window around the dock space draws before it, e.g. the
			// editor's titlebar, and they would cover it. Floating cards' shadows reach up to the
			// viewport's edge.
			const PainterRect viewportRect = RectAt(viewport.Pos, viewport.Size);
			const PainterRect reach = bInDockSpace ? Engine::Intersect(Engine::Grow(RectAt(window.Pos, window.Size), theme.CardGap), viewportRect) : viewportRect;
			drawList.PushClipRect(ToImVec2(reach.Min), ToImVec2(reach.Max), false);

			// A host of floating docked windows in an OS window of its own has nothing behind it
			// but the OS window: the backdrop goes there first, under ImGui's background.
			if (OwnsOsWindow(window))
			{
				ClearOsWindowBackground(window);
				WindowPainter& behind = GetPainterFor(drawList, viewport, window.Name, EWindowPainterPlacement::Behind);
				behind.AddRect(viewportRect, PainterStyle::Filled(theme.Backdrop));
			}

			Engine::PainterEffects shadow;
			shadow.Shadow = bInDockSpace ? theme.CardShadow : theme.FloatingCardShadow;

			WindowPainter& painter = GetPainterFor(drawList, viewport, window.Name);
			// One group: the cards cast one shadow together, under all of them, so no card's
			// shadow falls on its neighbour.
			painter.BeginGroup(shadow);
			// The glows of the windows the nodes show come first. In the group, they lie over its
			// shadow, which the shader draws before the group's first shape: light isn't darkened
			// by a shadow. Under every card, they show through the translucent ones. A glow casts
			// no shadow itself.
			for (const ImGuiDockNode* node : host.Cards)
			{
				if (node->VisibleWindow != nullptr)
				{
					PaintGlowsOf(painter, *node->VisibleWindow);
				}
			}
			for (const ImGuiDockNode* node : host.Cards)
			{
				// Windows docked together outside the dock space float, so their cards are like
				// floating windows': glass where they can be.
				if (bInDockSpace)
				{
					painter.AddRoundedRect(RectAt(node->Pos, node->Size), theme.CardRounding, PainterStyle::Filled(theme.Card));
				}
				else
				{
					PaintFloatingSurface(painter, window, RectAt(node->Pos, node->Size));
				}
			}
			painter.EndGroup();
			for (const ImGuiDockNode* node : host.Cards)
			{
				PaintCardBorder(painter, RectAt(node->Pos, node->Size), theme.CardRounding);
			}
			for (const ImGuiDockNode* node : host.Splits)
			{
				PaintSplitter(painter, *node);
			}
			// The tab bars on the cards, behind the labels ImGui draws into the next channel. A
			// docked tab bar has no ID of its own; its node's names it.
			for (const ImGuiDockNode* node : host.Cards)
			{
				if (node->TabBar != nullptr && node->TabBar->CurrFrameVisible == GImGui->FrameCount && !node->IsHiddenTabBar() && !node->IsNoTabBar())
				{
					PaintTabBar(painter, drawList, *node->TabBar, node->ID);
				}
			}

			drawList.PopClipRect();
			if (bTwoChannels)
			{
				drawList.ChannelsSetCurrent(channelBefore);
			}
		}

		bool GetsFloatingCard(const ImGuiWindow& window)
		{
			// Child windows lie inside a card; docked windows and dock hosts are their dock nodes' cards.
			const ImGuiWindowFlags notFloating = ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_DockNodeHost | ImGuiWindowFlags_NoBackground;
			return window.Active && !window.Hidden && !window.DockIsActive && window.Viewport != nullptr && (window.Flags & notFloating) == 0;
		}

		// A card behind everything the window draws, title bar included
		void PaintFloatingCard(ImGuiWindow& window)
		{
			const Theme& theme = GetTheme();
			ImDrawList& drawList = *window.DrawList;
			const ImGuiViewport& viewport = *window.Viewport;
			const PainterRect card = RectAt(window.Pos, window.Size);

			const PainterRect viewportRect = RectAt(viewport.Pos, viewport.Size);
			drawList.PushClipRect(ToImVec2(viewportRect.Min), ToImVec2(viewportRect.Max), false);
			WindowPainter& painter = GetPainterFor(drawList, viewport, window.Name, EWindowPainterPlacement::Behind);
			if (OwnsOsWindow(window))
			{
				// It fills its OS window, which leaves no room for a shadow. Windows rounds the OS
				// window's corners as the card's (VulkanImGuiBackend); the backdrop fills them where
				// it doesn't. ImGui's opaque background, drawn over all of it, is taken back.
				ClearOsWindowBackground(window);
				painter.AddRect(card, PainterStyle::Filled(theme.Backdrop));
				PaintFloatingSurface(painter, window, card);
				PaintCardBorder(painter, card, theme.CardRounding);
			}
			else
			{
				// The card's shadow, then the window's glows over it, as for docked cards, then the card
				Engine::PainterEffects shadow;
				shadow.Shadow = theme.FloatingCardShadow;
				painter.BeginGroup(shadow);
				PaintGlowsOf(painter, window);
				PaintFloatingSurface(painter, window, card);
				painter.EndGroup();
				PaintCardBorder(painter, card, theme.CardRounding);
			}
			drawList.PopClipRect();
		}

		// Tab bars in windows (ImGui::BeginTabBar()), painted behind everything their window
		// draws: after its card, if it has one, and before ImGui's tab labels. A tab bar in a child
		// window that draws a background of its own would be covered by it.
		void PaintWindowTabBars()
		{
			ImGuiContext& g = *GImGui;
			for (int index = 0; index < g.TabBars.GetMapSize(); ++index)
			{
				const ImGuiTabBar* tabBar = g.TabBars.TryGetMapData(index);
				if (tabBar == nullptr || tabBar->CurrFrameVisible != g.FrameCount || tabBar->Window == nullptr || !tabBar->Window->Active ||
					tabBar->Window->Hidden || tabBar->Window->Viewport == nullptr)
				{
					continue;
				}
				ImGuiWindow& window = *tabBar->Window;
				ImDrawList& drawList = *window.DrawList;
				drawList.PushClipRect(window.InnerClipRect.Min, window.InnerClipRect.Max, false);
				PaintTabBar(GetPainterFor(drawList, *window.Viewport, window.Name, EWindowPainterPlacement::Behind), drawList, *tabBar, tabBar->ID);
				drawList.PopClipRect();
			}
		}

		// A window's scrollbar along `axis` as a thin pill where ImGui's grab is. ImGui keeps the
		// scrollbar's behaviour and draws it transparent; the grab is laid out as ImGui::ScrollbarEx()
		// lays it out: along the scrollbar less its padding, as long as the share of the contents
		// shown, at the scroll position. Painted over the window's drawing, which ImGui keeps
		// clear of its scrollbars.
		void PaintScrollbar(ImGuiWindow& window, ImGuiAxis axis)
		{
			const ImGuiContext& g = *GImGui;
			const ImGuiStyle& style = g.Style;
			const Theme& theme = GetTheme();

			const ImRect frame = ImGui::GetWindowScrollbarRect(&window, axis);
			const float frameWidth = frame.GetWidth();
			const float frameHeight = frame.GetHeight();
			if (frameWidth <= 0.0f || frameHeight <= 0.0f)
			{
				return;
			}
			// As ImGui's: a vertical scrollbar shorter than twice its width fades out.
			const float alpha = axis == ImGuiAxis_Y && frameHeight < frameWidth ? ImSaturate(frameHeight / std::max(frameWidth * 2.0f, 1.0f)) : 1.0f;
			if (alpha <= 0.0f)
			{
				return;
			}
			ImRect track = frame;
			track.Expand(-IM_TRUNC(std::min(style.ScrollbarPadding, std::min(frameWidth, frameHeight) * 0.5f)));
			const float length = track.GetSize()[axis];
			if (length < 1.0f)
			{
				return;
			}

			// ImGui works in whole pixels here.
			const float visible = static_cast<float>(static_cast<ImS64>(window.InnerRect.Max[axis] - window.InnerRect.Min[axis]));
			const float contents = static_cast<float>(static_cast<ImS64>(window.ContentSize[axis] + window.WindowPadding[axis] * 2.0f));
			const float total = std::max(std::max(contents, visible), 1.0f);
			const float grabLength = ImClamp(length * (visible / total), std::min(length, style.GrabMinSize), length);
			const float scrollMax = std::max(1.0f, contents - visible);
			const float ratio = ImSaturate(static_cast<float>(static_cast<ImS64>(window.Scroll[axis])) / scrollMax);
			const float grabStart = track.Min[axis] + ratio * (length - grabLength);

			// Thin, wider while hovered, in the accent while dragged
			const ImGuiID id = ImGui::GetWindowScrollbarID(&window, axis);
			const bool bHeld = g.ActiveId == id;
			const float hovered = Animate(ImHashStr("Hovered", 0, id), g.HoveredId == id || bHeld ? 1.0f : 0.0f, theme.AnimationSpeed);
			const float held = Animate(ImHashStr("Held", 0, id), bHeld ? 1.0f : 0.0f, theme.AnimationSpeed);
			const ImGuiAxis across = axis == ImGuiAxis_X ? ImGuiAxis_Y : ImGuiAxis_X;
			const float thickness = std::min(glm::mix(theme.ScrollbarThumbWidth, theme.ScrollbarThumbHoveredWidth, hovered), track.GetSize()[across]);
			const float middle = (track.Min[across] + track.Max[across]) * 0.5f;
			ImRect thumb;
			thumb.Min[axis] = grabStart;
			thumb.Max[axis] = grabStart + grabLength;
			thumb.Min[across] = middle - thickness * 0.5f;
			thumb.Max[across] = middle + thickness * 0.5f;
			glm::vec4 color = glm::mix(glm::mix(theme.ScrollbarThumb, theme.ScrollbarThumbHovered, hovered), theme.ScrollbarThumbHeld, held);
			color.a *= alpha;

			ImDrawList& drawList = *window.DrawList;
			drawList.PushClipRect(frame.Min, frame.Max, false);
			GetPainterFor(drawList, *window.Viewport, window.Name).AddRoundedRect(RectAt(thumb.Min, thumb.GetSize()), thickness * 0.5f, PainterStyle::Filled(color));
			drawList.PopClipRect();
		}

		void PaintScrollbars()
		{
			for (ImGuiWindow* window : GImGui->Windows)
			{
				if (!window->Active || window->Hidden || window->Collapsed || window->Viewport == nullptr)
				{
					continue;
				}
				if (window->ScrollbarY)
				{
					PaintScrollbar(*window, ImGuiAxis_Y);
				}
				if (window->ScrollbarX)
				{
					PaintScrollbar(*window, ImGuiAxis_X);
				}
			}
		}
	}

	void SetPaintedChrome(bool bEnabled)
	{
		ChromeState& state = GetState();
		if (state.bEnabled == bEnabled || ImGui::GetCurrentContext() == nullptr)
		{
			return;
		}

		ImGuiStyle& style = ImGui::GetStyle();
		StyleBeforeChrome& before = state.StyleBefore;
		if (bEnabled)
		{
			for (size_t index = 0; index < std::size(PaintedColors); ++index)
			{
				before.ColorAlphas[index] = style.Colors[PaintedColors[index]].w;
				style.Colors[PaintedColors[index]].w = 0.0f;
			}
			before.WindowBorderSize = style.WindowBorderSize;
			before.PopupBorderSize = style.PopupBorderSize;
			before.DockingSeparatorSize = style.DockingSeparatorSize;

			// Cards have no borders, only their hairline.
			style.WindowBorderSize = 0.0f;
			style.PopupBorderSize = 0.0f;
			// Docked cards lie CardGap apart: ImGui leaves that much room between dock nodes,
			// where its splitter is.
			style.DockingSeparatorSize = GetTheme().CardGap;
		}
		else
		{
			for (size_t index = 0; index < std::size(PaintedColors); ++index)
			{
				style.Colors[PaintedColors[index]].w = before.ColorAlphas[index];
			}
			style.WindowBorderSize = before.WindowBorderSize;
			style.PopupBorderSize = before.PopupBorderSize;
			style.DockingSeparatorSize = before.DockingSeparatorSize;
		}
		state.bEnabled = bEnabled;
	}

	bool IsPaintedChromeEnabled()
	{
		return GetState().bEnabled;
	}

	void PaintBackdrop()
	{
		if (!GetState().bEnabled)
		{
			return;
		}
		const ImGuiWindow& window = *ImGui::GetCurrentWindowRead();
		const PainterRect rect = RectAt(window.Pos, window.Size);
		// The whole window, its padding included, which its own clip rect leaves out
		ImGui::PushClipRect(ToImVec2(rect.Min), ToImVec2(rect.Max), false);
		GetWindowPainter().AddRoundedRect(rect, window.WindowRounding, PainterStyle::Filled(GetTheme().Backdrop));
		ImGui::PopClipRect();
	}

	void AddWindowGlow(ImTextureRef texture, const PainterRect& rect, float reach, float strength)
	{
		if (!GetState().bEnabled || ImGui::GetCurrentContext() == nullptr || reach <= 0.0f || strength <= 0.0f)
		{
			return;
		}
		GetGlows().push_back(WindowGlow{ ImGui::GetCurrentWindowRead(), texture, rect, reach, strength });
	}

	void SetGlassBackdrop(ImTextureID backdrop)
	{
		GetState().GlassBackdrop = backdrop;
	}

	void SetPlainGlassBackdrop(ImTextureID backdrop)
	{
		GetState().PlainGlassBackdrop = backdrop;
	}

	std::span<const ImDrawList* const> GetGlassDrawLists()
	{
		return GetState().GlassDrawLists;
	}

	void PaintWindowCards()
	{
		// Last frame's glass windows are gone, whether or not there are cards this frame.
		GetState().GlassDrawLists.clear();
		if (!GetState().bEnabled || ImGui::GetCurrentContext() == nullptr)
		{
			return;
		}

		for (const DockHost& host : FindDockHosts())
		{
			PaintDockHost(host);
		}
		for (ImGuiWindow* window : GImGui->Windows)
		{
			if (GetsFloatingCard(*window))
			{
				PaintFloatingCard(*window);
			}
		}
		// After the floating cards, so a tab bar in a floating window lies on its card
		PaintWindowTabBars();
		PaintScrollbars();
	}
}

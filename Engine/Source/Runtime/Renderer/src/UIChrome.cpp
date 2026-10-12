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
		};

		// What the chrome changes in ImGui's style, as it was before, to put back when it goes off
		struct StyleBeforeChrome
		{
			float ColorAlphas[std::size(PaintedColors)] = {};
			float WindowBorderSize = 0.0f;
			float PopupBorderSize = 0.0f;
			float DockingSeparatorSize = 0.0f;
		};

		struct ChromeState
		{
			bool bEnabled = false;
			StyleBeforeChrome StyleBefore;
		};

		ChromeState& GetState()
		{
			static ChromeState state;
			return state;
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
			// Shadows reach past the dock space's edge, up to the viewport's
			const PainterRect viewportRect = RectAt(viewport.Pos, viewport.Size);
			drawList.PushClipRect(ToImVec2(viewportRect.Min), ToImVec2(viewportRect.Max), false);

			// A host of floating docked windows in an OS window of its own has nothing behind it
			// but the OS window: the backdrop goes there first.
			if (OwnsOsWindow(window))
			{
				WindowPainter& behind = GetPainterFor(drawList, viewport, window.Name, EWindowPainterPlacement::Behind);
				behind.AddRect(viewportRect, PainterStyle::Filled(theme.Backdrop));
			}

			// Docked into the dock space, or floating together
			ImGuiDockNode* someNode = host.Cards.empty() ? host.Splits.front() : host.Cards.front();
			const bool bInDockSpace = ImGui::DockNodeGetRootNode(someNode)->IsDockSpace();
			const glm::vec4& cardColor = bInDockSpace ? theme.Card : theme.FloatingCard;
			Engine::PainterEffects shadow;
			shadow.Shadow = bInDockSpace ? theme.CardShadow : theme.FloatingCardShadow;

			WindowPainter& painter = GetPainterFor(drawList, viewport, window.Name);
			// One group: the cards cast one shadow together, under all of them, so no card's
			// shadow falls on its neighbour.
			painter.BeginGroup(shadow);
			for (const ImGuiDockNode* node : host.Cards)
			{
				painter.AddRoundedRect(RectAt(node->Pos, node->Size), theme.CardRounding, PainterStyle::Filled(cardColor));
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
				// It fills its OS window, which leaves no room for round corners or a shadow.
				painter.AddRect(card, PainterStyle::Filled(theme.FloatingCard));
			}
			else
			{
				PainterStyle style = PainterStyle::Filled(theme.FloatingCard);
				style.Effects.Shadow = theme.FloatingCardShadow;
				painter.AddRoundedRect(card, theme.CardRounding, style);
				PaintCardBorder(painter, card, theme.CardRounding);
			}
			drawList.PopClipRect();
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

	void PaintWindowCards()
	{
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
	}
}

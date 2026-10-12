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
	// "Floating" over them, and "Overlay", which asks for no background.
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
				ImGui::DockBuilderDockWindow("Right", RightNode);
				ImGui::DockBuilderFinish(dockSpace);
			}
			ImGui::DockSpace(dockSpace);
			ImGui::End();

			for (const char* name : { "Left", "Right" })
			{
				ImGui::Begin(name);
				ImGui::TextUnformatted(name);
				ImGui::End();
			}

			ImGui::SetNextWindowPos(ImVec2(300.0f, 200.0f));
			ImGui::SetNextWindowSize(ImVec2(200.0f, 120.0f));
			ImGui::Begin("Floating", nullptr, ImGuiWindowFlags_NoDocking);
			ImGui::TextUnformatted("Floating");
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
		Require(cards != nullptr && CommandsOfType(*cards, EPainterCommandType::Line).empty(), "A splitter nobody hovers should not show");

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
		const std::vector<PainterCommand> lines = cards != nullptr ? CommandsOfType(*cards, EPainterCommandType::Line) : std::vector<PainterCommand>{};
		Require(lines.size() == 1, "A hovered splitter should show as one line; ImGui may have changed how it makes the splitter's ID");
		const auto line = GetPayload<PainterLinePayload>(lines[0]);
		Require(Near(line.From.x, gapX) && Near(line.To.x, gapX) && line.From.y < middleY && line.To.y > middleY, "The line should run upright along the middle of the gap");

		ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
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

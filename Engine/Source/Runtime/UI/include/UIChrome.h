#pragma once

#include "PainterCommands.h"

#include <imgui.h>

#include <span>

namespace Nyx::UI
{
	// The painted window chrome: every window painted as a card on a backdrop. ImGui keeps the
	// layout, docking and input; it only stops drawing what the chrome paints instead.

	// Turns the painted chrome on or off. On, ImGui's window backgrounds and the rest the chrome
	// paints become transparent, and ImGui leaves the gap between dock nodes; off, ImGui's style
	// is as it was. Takes effect for windows begun after the call, so a frame in which it changes
	// can look mixed.
	void SetPaintedChrome(bool bEnabled);
	bool IsPaintedChromeEnabled();

	// How narrow a docked window may get, in pixels: next to nothing, so a panel can be docked as
	// a sliver. What it holds is cut off at its edge rather than holding it wide.
	constexpr float DockedWindowMinWidth = 10.0f;

	// ImGui::DockSpace(), with docked windows as narrow as DockedWindowMinWidth. ImGui keeps the
	// dock space's nodes at least style.WindowMinSize wide, which is for floating windows; it
	// takes the size while the dock space is updated, so it is changed only for that.
	ImGuiID DockSpace(ImGuiID id, const ImVec2& size = ImVec2(0.0f, 0.0f));

	// After ImGui's style colours were set again while the chrome is on, e.g. for another theme
	// (ApplyThemeToImGui()): keeps them as the colours to put back when the chrome goes off, makes
	// those the chrome paints transparent again, and takes the gap between docked cards from the
	// theme. Without it, ImGui would fill the windows and the gaps between them again. Nothing
	// while the chrome is off.
	void ReapplyPaintedChrome();

	// Paints the backdrop over the whole window being built, behind everything it draws next:
	// call right after the window's Begin(), e.g. the dock space's host window. Nothing while the
	// chrome is off.
	void PaintBackdrop();

	// For a window that fills the main viewport behind the other windows, such as the game's
	// picture: call right after its Begin(). Such a window has
	// ImGuiWindowFlags_NoBringToFrontOnFocus, and ImGui puts a window with that flag behind every
	// window there is when it first appears, also behind the dock space's host window, whose
	// backdrop then covers it. This puts it in front of the windows there are, once, when it
	// appears; windows made after it stay in front of it, also when it is clicked.
	void ShowAboveBackdrop();

	// Light from a picture onto the editor around the window being built (ambilight): an image
	// glow of `texture` around `rect`, in ImGui's screen coordinates, as Painter::AddImageGlow()
	// takes it. Call it between the window's own Begin() and End(), not in a child window, each
	// frame it should glow. Nothing while the chrome is off, nor for a window that fills an OS
	// window of its own, which has no room around it.
	void AddWindowGlow(ImTextureRef texture, const PainterRect& rect, float reach, float strength);

	// The picture glass shows: what lies behind the glass windows in the main window, blurred,
	// as large as the main window. The renderer sets it before PaintWindowCards(), or
	// ImTextureID_Invalid when it has none yet.
	void SetGlassBackdrop(ImTextureID backdrop);

	// The picture glass shows in OS windows of their own, which are opaque to the OS and so can't
	// show what lies behind them: one colour, the theme's backdrop (one pixel is enough). Until it
	// is set (ImTextureID_Invalid), their cards are opaque.
	void SetPlainGlassBackdrop(ImTextureID backdrop);

	// The draw lists of the windows that are glass this frame, or would be with a backdrop: all
	// in the main viewport. Everything ImGui renders there before the first of them is what the
	// glass shows. Empty when there is no glass; valid after PaintWindowCards() until the next.
	std::span<const ImDrawList* const> GetGlassDrawLists();

	// Paints the cards, tab bars and scrollbars of all windows of this frame. Call it once the
	// frame has ended (ImGui::EndFrame(), so ImGui's own windows are there too) and before the
	// window painters are gathered (EndWindowPainters()). Nothing while the chrome is off.
	void PaintWindowCards();
}

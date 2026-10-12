#pragma once

#include "PainterCommands.h"

#include <imgui.h>

#include <span>

namespace Nyx::UI
{
	// The painted window chrome (Design/ui-painter.md, stage 3, change 3): a backdrop behind the
	// dock space, and every window painted as a card on it. ImGui keeps doing the layout, the
	// docking and the input; it only stops drawing window backgrounds, borders, title bar
	// backgrounds, resize grips and splitters, which the chrome paints instead.
	//
	// - Docked windows: one card per dock node, exactly the node's rect. ImGui leaves the theme's
	//   CardGap between dock nodes (its DockingSeparatorSize), so cards stand apart and the
	//   splitter between them is the gap. A splitter shows as a thin line while hovered or dragged.
	// - Floating windows, popups, menus and tooltips: a card behind each, lighter and with a
	//   stronger shadow. A window that fills an OS window of its own gets a plain one, as the OS
	//   window has no room around it for a shadow or round corners.
	// - A window begun with ImGuiWindowFlags_NoBackground gets no card.
	// - Tab bars, docked and in windows (stage 3, change 4): ImGui keeps the tabs' behaviour,
	//   labels and close buttons; their shapes are painted behind the labels instead: a pill
	//   behind the selected tab and a hovered one, and an accent underline under the selected tab
	//   that slides to the next one selected.
	// - Glass (stage 3, change 6): floating cards in the main window are glass, showing what
	//   lies behind them, blurred. The renderer draws that picture each frame there is glass: all
	//   ImGui draws in the main window before the first glass window (GetGlassDrawLists()),
	//   blurred, handed over with SetGlassBackdrop(). Until it is there, the cards are opaque.
	//   Windows in OS windows of their own can't see what lies behind them and stay opaque.
	// - Window glows, for ambilight (stage 3, change 5): a window can glow with a picture onto
	//   the backdrop around it (AddWindowGlow()). The glows are painted behind every card, and
	//   docked cards are slightly translucent, so a glow shows faintly through its neighbours.
	//
	// The colours, rounding, gap and shadows are the theme's (UITheme.h).

	// Turns the painted chrome on or off. On, ImGui's window backgrounds and the rest the chrome
	// paints become transparent, and ImGui leaves the gap between dock nodes; off, ImGui's style
	// is as it was. Takes effect for windows begun after the call, so a frame in which it changes
	// can look mixed.
	void SetPaintedChrome(bool bEnabled);
	bool IsPaintedChromeEnabled();

	// Paints the backdrop over the whole window being built, behind everything it draws next:
	// call right after the window's Begin(), e.g. the dock space's host window. Nothing while the
	// chrome is off.
	void PaintBackdrop();

	// Light from a picture onto the editor around the window being built (ambilight): an image
	// glow of `texture` around `rect`, in ImGui's screen coordinates, e.g. where the window shows
	// the picture. It reaches `reach` pixels out and starts at `strength`, from 0 to 1
	// (Painter::AddImageGlow()); a blurred picture glows softly. The chrome paints it with the
	// window's card, behind all cards. Call it between the window's own Begin() and End(), not
	// in a child window, each frame it should glow. Nothing while the chrome is off, nor for a window that fills an OS
	// window of its own, which has no room around it.
	void AddWindowGlow(ImTextureRef texture, const Engine::PainterRect& rect, float reach, float strength);

	// The picture glass shows: what lies behind the glass windows in the main window, blurred,
	// as large as the main window. The renderer sets it before PaintWindowCards(), or
	// ImTextureID_Invalid when it has none yet.
	void SetGlassBackdrop(ImTextureID backdrop);

	// The draw lists of the windows that are glass this frame, or would be with a backdrop: all
	// in the main viewport. Everything ImGui renders there before the first of them is what the
	// glass shows. Empty when there is no glass; valid after PaintWindowCards() until the next.
	std::span<const ImDrawList* const> GetGlassDrawLists();

	// Paints the cards and tab bars of all windows of this frame. Call it once the frame has ended
	// (ImGui::EndFrame(), so ImGui's own windows are there too) and before the window painters
	// are gathered (EndWindowPainters()). The renderer does. Nothing while the chrome is off.
	void PaintWindowCards();
}

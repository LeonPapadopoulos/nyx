#pragma once

#include <imgui.h>

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

	// Paints the cards and tab bars of all windows of this frame. Call it once the frame has ended
	// (ImGui::EndFrame(), so ImGui's own windows are there too) and before the window painters
	// are gathered (EndWindowPainters()). The renderer does. Nothing while the chrome is off.
	void PaintWindowCards();
}

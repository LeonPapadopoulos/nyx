#pragma once

#include "PainterTiles.h"

#include <cstdint>

namespace Nyx::UI
{
	// Finds what ImGui still draws itself, to see what isn't painted yet (Design/ui-painter.md,
	// stage 3, changes 8 and 9). With marking on, each shape ImGui draws (its rects, lines,
	// circles and triangles, in any window or in a viewport's background or foreground draw list)
	// is outlined in red, over everything in its OS window. Shapes ImGui draws fully transparent
	// don't show, and aren't marked.
	//
	// ImGui draws shapes and text from the same texture, its font atlas: shapes read the atlas's
	// white pixel or its baked lines, glyphs the rest. So a triangle all of whose corners read
	// those is a shape. ImGui's text, which stays (the painter's goal leaves text to ImGui), and
	// its images aren't marked. A shape is the triangles ImGui adds for one primitive: they use
	// vertices of their own, after the previous primitive's.

	void SetMarkImGuiShapes(bool bEnabled);
	bool IsMarkingImGuiShapes();

	// Marks this frame's ImGui shapes with the foreground painters (GetForegroundPainter()). Call
	// once the frame has ended and the chrome is painted (UIChrome.h), before the window painters
	// are gathered; the renderer does. Nothing while marking is off.
	void MarkImGuiShapes();

	// How many shapes ImGui drew in the frame marked last, and in how many windows (a viewport's
	// background or foreground draw list counts as one); 0 while marking is off
	uint32_t GetImGuiShapeCount();
	uint32_t GetImGuiShapeWindowCount();

	// The painter heatmap (Design/ui-painter.md, stage 3, change 11): over each OS window, where
	// the painter's work lies, from the heat the renderer sums per tile over all draws
	// (Engine::PainterHeatGrid). It is drawn with ImGui's draw list, not the painter, so that it
	// doesn't add to the painting it measures.
	enum class EPainterHeatmap
	{
		Off,
		// The commands each tile's pixels walk, summed over the draws covering it: blue for a few,
		// through green, to red for PainterHeatmapCommandsRed or more, as the canvas's heatmap
		Commands,
		// How many draws cover each tile: blue for one, through green, to red for
		// PainterHeatmapDrawsRed or more
		Draws,
	};
	constexpr uint32_t PainterHeatmapCommandsRed = 16;
	constexpr uint32_t PainterHeatmapDrawsRed = 8;

	void SetPainterHeatmap(EPainterHeatmap heatmap);
	EPainterHeatmap GetPainterHeatmap();

	// Asks the renderer to estimate, this frame, what the per-tile reduction would keep of the
	// painter's tile entries (Engine::PainterWindowStats::Occlusion, stage 3, change 12): call each
	// frame the estimate is shown. It walks every entry, so it is only made while asked for.
	void RequestPainterEstimates();
	bool ArePainterEstimatesRequested();

	// Shows the heat of `stats`' OS windows (the frame before, as the renderer sums it once ImGui
	// has made the draw data) over them, in their foreground draw lists, with a legend in each
	// one's top right corner and the tile's counts beside the mouse. Call once the frame has ended
	// and the shapes are marked; the renderer does. Nothing while the heatmap is off.
	void ShowPainterHeatmap(const Engine::PainterWindowStats& stats);
}

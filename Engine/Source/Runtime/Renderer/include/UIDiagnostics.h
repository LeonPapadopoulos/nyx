#pragma once

#include <cstdint>

namespace Nyx::UI
{
	// Finds what ImGui still draws itself, to see what isn't painted yet (Design/ui-painter.md,
	// stage 3, change 8). With marking on, each shape ImGui draws (its rects, lines, circles and
	// triangles, in any window) is outlined in red, over everything in its OS window.
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

	// How many shapes ImGui drew in the frame marked last, and in how many windows; 0 while
	// marking is off
	uint32_t GetImGuiShapeCount();
	uint32_t GetImGuiShapeWindowCount();
}

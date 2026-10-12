#pragma once

#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx::Engine
{
	class Painter;

	// The size of the test sheet, in its own units: pixels at a zoom of 100%
	constexpr glm::vec2 PainterTestSheetSize{ 760.0f, 560.0f };

	// Draws a sheet with every primitive the painter has, in rows with labels: rects, circles and
	// sectors, lines, triangles, shadows and glows, text and images. The editor's Painter Test
	// window shows it (Design/ui-painter.md, change 5); a primitive added to the painter belongs
	// on it too.
	//
	// Draws in the painter's current transform, so a caller zooms and pans the sheet with
	// PushTransform(). `font` is used for the labels and the text samples, and `image` (anything
	// ImGui::Image() takes) for the image samples.
	void DrawPainterTestSheet(Painter& painter, ImFont* font, ImTextureRef image);
}

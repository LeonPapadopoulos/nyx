#pragma once

#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx::Engine
{
	class Painter;

	// The size of the test sheet, in its own units: pixels at a zoom of 100%
	constexpr glm::vec2 PainterTestSheetSize{ 760.0f, 560.0f };

	// The sheet's rows: rects, circles and sectors, lines, triangles, shadows and glows, text and
	// images. Row `row` covers PainterTestSheetRowHeight from GetPainterTestSheetRowTop(row) down.
	constexpr int PainterTestSheetRowCount = 6;
	constexpr float PainterTestSheetRowHeight = 80.0f;
	constexpr float GetPainterTestSheetRowTop(int row)
	{
		return 16.0f + PainterTestSheetRowHeight * static_cast<float>(row);
	}

	// Draws a sheet with every primitive the painter has, in rows with labels. The editor's
	// Painter Test window shows it (Design/ui-painter.md, stage 1, change 5); a primitive added
	// to the painter belongs on it too.
	//
	// Draws in the painter's current transform, so a caller zooms and pans the sheet with
	// PushTransform(). `font` is used for the labels and the text samples, and `image` (anything
	// ImGui::Image() takes) for the image samples.
	void DrawPainterTestSheet(Painter& painter, ImFont* font, ImTextureRef image);

	// Draws one row of the sheet, label included, where it lies on the sheet. `TPainter` is
	// Painter or Nyx::UI::WindowPainter, which paints the sheet straight into an ImGui window.
	template<typename TPainter>
	void DrawPainterTestSheetRow(TPainter& painter, int row, ImFont* font, ImTextureRef image);
}

#pragma once

#include "ImageFile.h"

#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx::UI
{
	class Painter;

	// The size of the test sheet, in its own units: pixels at a zoom of 100%
	constexpr glm::vec2 PainterTestSheetSize{ 760.0f, 720.0f };

	// The sheet's rows: rects, circles and sectors, lines, triangles, shadows and glows, image
	// glows, glass, text and images. Row `row` covers PainterTestSheetRowHeight from
	// GetPainterTestSheetRowTop(row) down; the last row is taller and fills the sheet.
	constexpr int PainterTestSheetRowCount = 8;
	constexpr float PainterTestSheetRowHeight = 80.0f;
	constexpr float GetPainterTestSheetRowTop(int row)
	{
		return 16.0f + PainterTestSheetRowHeight * static_cast<float>(row);
	}

	// Draws a sheet with every primitive the painter has, in rows with labels. The editor's
	// Painter Test window shows it; a primitive added to the painter belongs on it too.
	//
	// Draws in the painter's current transform, so a caller zooms and pans the sheet with
	// PushTransform(). `font` is used for the labels and the text samples, `image` (anything
	// ImGui::Image() takes) for the image samples, and `gradient` for the image glows and as
	// the picture behind the glass: a texture made from MakePainterTestGradient().
	void DrawPainterTestSheet(Painter& painter, ImFont* font, ImTextureRef image, ImTextureRef gradient);

	// Draws one row of the sheet, label included, where it lies on the sheet. `TPainter` is
	// Painter or Nyx::UI::WindowPainter, which paints the sheet straight into an ImGui window.
	template<typename TPainter>
	void DrawPainterTestSheetRow(TPainter& painter, int row, ImFont* font, ImTextureRef image, ImTextureRef gradient);

	// The image the sheet's image glows show: a different colour in each corner, blended in
	// between, so each side of a glow has colours of its own
	RgbaImage MakePainterTestGradient();
}

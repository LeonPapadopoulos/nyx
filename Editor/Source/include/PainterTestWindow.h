#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Editor
{
	// The "Painter Test" window (Debug > Painter Test): the painter's test sheet, painted on the
	// GPU and shown pixel for pixel. The wheel zooms around the mouse and dragging pans, so edges
	// can be checked at any scale. It shows the tile heatmap, the painter's statistics and
	// problems, and on request the same frame drawn by ImGui's ImDrawList beside it, for
	// comparison (Design/ui-painter.md, change 5).
	class PainterTestWindow
	{
	public:
		void Draw(Nyx::IRenderer& renderer);

		// Gives the canvas back to the renderer. Done when the window closes; call it before the
		// renderer shuts down too.
		void Release();

		bool bOpen = false;

	private:
		void DrawToolbar();
		void DrawStatistics() const;

		// The wheel zooms around the mouse; dragging with any button pans. Works on the item
		// drawn last, an invisible button over the canvas.
		void HandleCanvasInput(const ImVec2& canvasMin);

		Nyx::IRenderer* Renderer = nullptr;
		uint64_t CanvasId = 0;
		Nyx::Engine::Painter Painter;

		float Zoom = 1.0f;
		// Where the sheet's top left is in the canvas, in pixels
		glm::vec2 Pan{ 16.0f, 16.0f };

		bool bHeatmap = false;
		bool bCompareWithImDrawList = false;
	};

	// Draws a painter's recorded frame with ImGui's ImDrawList instead, for comparison: shapes
	// become ImGui's triangles, glyphs and images ImGui's textured quads. `origin` is where the
	// frame's top left goes on the screen. ImDrawList has no shadows, glows or per-corner radii;
	// shadows and glows are left out, and a rect gets its largest corner radius.
	void DrawPainterFrameWithImDrawList(const Nyx::Engine::Painter& painter, ImDrawList& drawList, ImVec2 origin);
}

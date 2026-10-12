#pragma once

#include "Painter.h"
#include "PainterTiles.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <string>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Editor
{
	// The "Painter Test" window (Debug > Painter Test), with two tabs (Design/ui-painter.md):
	//  - Canvas: the painter's test sheet, painted on the GPU into an image and shown pixel for
	//    pixel. The wheel zooms around the mouse and dragging pans, so edges can be checked at any
	//    scale. It shows the tile heatmap, the painter's statistics and problems, and on request
	//    the same frame drawn by ImGui's ImDrawList beside it, for comparison (stage 1, change 5).
	//  - In a window: the same sheet painted straight into a scrolling child window, row by row,
	//    with an ImGui button over each row and a painted badge over each button, to show the
	//    order; with the statistics of all painting inside windows (stage 2, change 4).
	//  - Widgets, the UI gallery: each painted widget (UIWidgets.h) in every state, forced, then
	//    the live widget and ImGui's counterpart beside it, at 100% and 150% font size. It is
	//    where widgets are designed (stage 2, change 5).
	//  - Blur: a scene view's picture beside its blur (VulkanBlur), as ImGui shows it and painted,
	//    with the time all blurs take on the GPU (stage 3, change 1).
	class PainterTestWindow
	{
	public:
		void Draw(Nyx::IRenderer& renderer);

		// Gives the canvas back to the renderer. Done when the window closes; call it before the
		// renderer shuts down too.
		void Release();

		bool bOpen = false;

	private:
		void DrawCanvasTab(Nyx::IRenderer& renderer);
		void DrawInWindowTab(Nyx::IRenderer& renderer);
		void DrawWidgetsTab(Nyx::IRenderer& renderer);
		// A scene view's picture beside its blur, shown by ImGui and painted, with the blur's GPU time
		void DrawBlurTab(Nyx::IRenderer& renderer);
		void DrawWidgetGalleryTable();
		// Saves the gallery as a PNG file in Saved/Gallery, named by the time
		void SaveGalleryPng(Nyx::IRenderer& renderer);

		// Takes up `size` at ImGui's cursor and returns where, for painting a forced state there
		static Nyx::Engine::PainterRect ReserveCell(glm::vec2 size);

		void DrawToolbar();
		void DrawStatistics() const;
		void DrawWindowPainterStatistics(const Nyx::Engine::PainterWindowStats& stats) const;

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

		// The gallery's live widgets' values, shared with their ImGui counterparts
		bool bGalleryToggle = true;
		float GallerySlider = 0.6f;

		// Where the gallery showed last frame, for Save PNG, and why it couldn't be saved
		ImVec2 GalleryMin;
		ImVec2 GalleryMax;
		bool bGalleryInMainWindow = false;
		std::string GalleryMessage;

		// The scene view the Blur tab shows, an index into the renderer's scene views; the largest
		// until one is chosen
		size_t BlurViewIndex = 0;
		bool bBlurViewChosen = false;
	};

	// Draws a painter's recorded frame with ImGui's ImDrawList instead, for comparison: shapes
	// become ImGui's triangles, glyphs and images ImGui's textured quads. `origin` is where the
	// frame's top left goes on the screen. ImDrawList has no shadows, glows or per-corner radii;
	// shadows and glows are left out, and a rect gets its largest corner radius. Glyphs and images
	// on a texture ImGui replaced this frame (its font atlas grew) are left out for that frame.
	void DrawPainterFrameWithImDrawList(const Nyx::Engine::Painter& painter, ImDrawList& drawList, ImVec2 origin);
}

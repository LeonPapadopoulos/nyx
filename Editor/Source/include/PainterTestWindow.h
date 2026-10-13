#pragma once

#include "Painter.h"
#include "PainterTiles.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Editor
{
	// The "Painter Test" window (Debug > Painter Test), with these tabs (Design/ui-painter.md):
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
	//  - Statistics: what painting the whole editor costs, on the CPU and the GPU, the last frame,
	//    on average and at its peak over the last frames, per OS window, and as a painted graph
	//    (stage 3, change 10). Kept while the window is open, whichever tab shows.
	class PainterTestWindow
	{
	public:
		void Draw(Nyx::IRenderer& renderer);

		// Gives the canvas and the gradient image back to the renderer. Done when the window
		// closes; call it before the renderer shuts down too.
		void Release();

		bool bOpen = false;

	private:
		void DrawCanvasTab(Nyx::IRenderer& renderer);
		void DrawInWindowTab(Nyx::IRenderer& renderer);
		void DrawWidgetsTab(Nyx::IRenderer& renderer);
		// A scene view's picture beside its blur, shown by ImGui and painted, with the blur's GPU time
		void DrawBlurTab(Nyx::IRenderer& renderer);
		void DrawStatisticsTab(Nyx::IRenderer& renderer);
		void DrawWidgetGalleryTable();
		// Saves the gallery as a PNG file in Saved/Gallery, named by the time
		void SaveGalleryPng(Nyx::IRenderer& renderer);

		// Takes up `size` at ImGui's cursor and returns where, for painting a forced state there
		static Nyx::Engine::PainterRect ReserveCell(glm::vec2 size);

		// The test sheet's gradient image (MakePainterTestGradient()), made the first time it's asked for
		ImTextureRef GetGradientTexture(Nyx::IRenderer& renderer);

		void DrawToolbar();
		void DrawStatistics() const;
		void DrawWindowPainterStatistics(const Nyx::Engine::PainterWindowStats& stats) const;

		// The wheel zooms around the mouse; dragging with any button pans. Works on the item
		// drawn last, an invisible button over the canvas.
		void HandleCanvasInput(const ImVec2& canvasMin);

		Nyx::IRenderer* Renderer = nullptr;
		uint64_t CanvasId = 0;
		uint64_t GradientImageId = 0;
		Nyx::Engine::Painter Painter;

		float Zoom = 1.0f;
		// Where the sheet's top left is in the canvas, in pixels
		glm::vec2 Pan{ 16.0f, 16.0f };

		bool bHeatmap = false;
		bool bCompareWithImDrawList = false;

		// The gallery's live widgets' values, shared with their ImGui counterparts
		bool bGalleryToggle = true;
		float GallerySlider = 0.6f;
		bool bGalleryRowSelected = false;
		std::string GalleryText = "Cube";
		std::string GalleryLines = "#0 Main()\n#1 Run()";
		std::string GallerySearch;
		float GalleryNumber = 1.25f;
		glm::vec3 GalleryVector{ 1.0f, 2.0f, 3.0f };
		int GalleryComboIndex = 0;
		int GalleryTableRow = 1;
		glm::vec3 GalleryColor{ 0.35f, 0.65f, 0.95f };

		// Where the gallery showed last frame, for Save PNG, and why it couldn't be saved
		ImVec2 GalleryMin;
		ImVec2 GalleryMax;
		bool bGalleryInMainWindow = false;
		std::string GalleryMessage;

		// The scene view the Blur tab shows, an index into the renderer's scene views; the largest
		// until one is chosen
		size_t BlurViewIndex = 0;
		bool bBlurViewChosen = false;

		// The Statistics tab's last frames, oldest first once full: each frame's times in
		// milliseconds, and its draws and commands
		struct FrameSample
		{
			float Frame = 0.0f;
			// The painter's CPU time after the UI and its GPU time, one after the other
			float PainterTotal = 0.0f;
			float BuildUI = 0.0f;
			float Chrome = 0.0f;
			float Gather = 0.0f;
			float ImGuiRender = 0.0f;
			float Segments = 0.0f;
			float Binning = 0.0f;
			float PainterCpu = 0.0f;
			float GpuPainter = 0.0f;
			float GpuMainWindowUI = 0.0f;
			float GpuGlassBackdrop = 0.0f;
			float GpuBlur = 0.0f;
			float Draws = 0.0f;
			float Commands = 0.0f;
			float UploadBytes = 0.0f;
			float CommandUploadBytes = 0.0f;
			// Tile entries, and what the per-tile reduction would keep: only while estimated
			float Entries = 0.0f;
			float Kept = 0.0f;
			float KeptCardsOpaque = 0.0f;
			bool bEstimated = false;
		};
		static constexpr size_t StatisticsFrames = 240;
		std::vector<FrameSample> FrameSamples;
		size_t NextFrameSample = 0;
		void RecordFrameSample(const Nyx::IRenderer& renderer);
		// The frames kept, oldest first
		std::vector<FrameSample> GetFrameSamples() const;
		void DrawOsWindowStatistics(const Nyx::Engine::PainterWindowStats& stats) const;
		// The times of the frames kept as lines over a field, painted, the newest at the right
		void DrawFrameGraph(std::span<const FrameSample> samples) const;
		// The frame now and after each proposed change (PredictPainterScenarios()), from the
		// frames kept that have the reduction's estimate
		void DrawPredictions(std::span<const FrameSample> samples) const;
	};

	// What the painter costs a frame on average, measured (the Statistics tab), for predicting
	// what proposed changes would bring (Design/ui-painter.md, stage 3, change 12)
	struct PainterCosts
	{
		// Milliseconds: the editor's frame, the painter's CPU time after the UI is built, the
		// binning in it, and the painter's draws on the GPU
		float Frame = 0.0f;
		float PainterCpu = 0.0f;
		float Binning = 0.0f;
		float PainterGpu = 0.0f;
		// Tile entries, and what the per-tile reduction would keep of them, with the docked cards
		// as they are and opaque (PainterOcclusionEstimate)
		float Entries = 0.0f;
		float Kept = 0.0f;
		float KeptCardsOpaque = 0.0f;
		// Uploaded per frame, and the commands' and groups' part of it
		float UploadBytes = 0.0f;
		float CommandUploadBytes = 0.0f;
	};

	// One change's predicted frame
	struct PainterScenario
	{
		const char* Name = "";
		float PainterCpu = 0.0f;
		float PainterGpu = 0.0f;
		float UploadBytes = 0.0f;
		float Frame = 0.0f;
	};

	// What binning on the GPU would cost there, assumed until it is built: nanoseconds per tile
	// entry (counting and writing it), and milliseconds for its dispatches. For a GPU like the
	// RTX 3070 Ti; an integrated GPU, several times as much.
	constexpr float AssumedGpuBinningNanosecondsPerEntry = 0.2f;
	constexpr float AssumedGpuBinningDispatchMilliseconds = 0.05f;

	// Predicts the frame now and after each proposed change, from measured costs. The model:
	//  - The per-tile reduction drops the entries under opaque shapes. The painter's GPU time
	//    goes down with the entries its pixels walk (as if dropped and kept commands cost the
	//    same; shadows, glows and image glows cost more, so it is cautious there), and the upload
	//    with the entries. Binning on the CPU stays: it still walks every command's tiles.
	//  - With the docked cards opaque as well, which ambilight through them would need another way.
	//  - Binning on the GPU: the CPU's binning goes, and the tiles and entries are no longer
	//    uploaded; the GPU bins all entries, at the assumed cost above.
	//  - Both: the reduction (cards opaque) with binning on the GPU, which bins all entries
	//    before dropping any.
	// The editor's frame changes by what the painter's CPU and GPU times change: it runs one frame
	// at a time, so they add up.
	std::vector<PainterScenario> PredictPainterScenarios(const PainterCosts& now);

	// Draws a painter's recorded frame with ImGui's ImDrawList instead, for comparison: shapes
	// become ImGui's triangles, glyphs and images ImGui's textured quads. `origin` is where the
	// frame's top left goes on the screen. ImDrawList has no shadows, glows or per-corner radii;
	// shadows, glows and image glows are left out, and a rect gets its largest corner radius.
	// Glyphs and images on a texture ImGui replaced this frame (its font atlas grew) are left out
	// for that frame.
	void DrawPainterFrameWithImDrawList(const Nyx::Engine::Painter& painter, ImDrawList& drawList, ImVec2 origin);
}

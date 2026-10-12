#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::UI
{
	// Paints into an ImGui window, in order with ImGui's own drawing there: a shape painted after
	// an ImGui widget lands in front of it, and the next widget in front of the shape. The plan is
	// in Design/ui-painter.md, stage 2.
	//
	// It records like a Painter, but in ImGui's screen coordinates, as ImDrawList does:
	//
	//     windowPainter.Begin(*ImGui::GetWindowDrawList(), *ImGui::GetWindowViewport());
	//     windowPainter.AddRoundedRect(card, 6.0f, PainterStyle::Filled(cardColor)); // behind the text
	//     ImGui::TextUnformatted("Hello");
	//     windowPainter.AddCircle(badgeCenter, 4.0f, PainterStyle::Filled(accent)); // in front of it
	//     windowPainter.End();
	//
	// How it keeps the order: the shapes are cut into segments (PainterSegment) wherever ImGui has
	// drawn in between, and each segment starts with a callback in the window's draw list. ImGui
	// renders a draw list in order, so the callback, where the renderer draws the segment, comes
	// after everything ImGui drew before the shapes and before everything it draws after them.
	//
	// Painting is clipped like ImGui's drawing: each shape gets ImGui's current clip rect, so
	// ImGui::PushClipRect(), child windows, tables and scrolling regions clip it too. A new clip
	// rect starts a new segment, so all shapes of a segment share one clip rect.
	//
	// Shapes are recorded in the viewport's framebuffer pixels: ImGui positions minus the
	// viewport's position, times its framebuffer scale. Those are the pixels the segments are
	// drawn into.
	//
	// The draw list keeps a pointer to this window painter until ImGui renders it, so it must not
	// move or be destroyed before then. Like Painter, it never logs or asserts; misuse is collected
	// in GetProblems().
	class WindowPainter
	{
	public:
		// Starts a frame of painting into `drawList`, a window's, which ImGui renders into `viewport`.
		// Drops the previous frame's shapes, segments and problems.
		void Begin(ImDrawList& drawList, const ImGuiViewport& viewport);

		// Ends the frame: closes what was left open and adds the painter's problems to this one's
		void End();

		// Shapes, as Painter's, in ImGui's screen coordinates. Each one starts a new segment if
		// ImGui has drawn, or changed its clip rect, since the current segment started.

		void AddRect(const Engine::PainterRect& rect, const Engine::PainterStyle& style);
		void AddRoundedRect(const Engine::PainterRect& rect, float radius, const Engine::PainterStyle& style);
		void AddRoundedRect(const Engine::PainterRect& rect, const Engine::PainterCornerRadii& radii, const Engine::PainterStyle& style);
		void AddCircle(glm::vec2 center, float radius, const Engine::PainterStyle& style);
		void AddCircleSector(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, const Engine::PainterStyle& style);
		void AddArc(glm::vec2 center, float radius, float width, float startAngle, float endAngle, const Engine::PainterStyle& style);
		void AddLine(glm::vec2 from, glm::vec2 to, float width, Engine::EPainterLineCap cap, const Engine::PainterStyle& style);
		void AddTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Engine::PainterStyle& style);
		void AddText(ImFont* font, float size, glm::vec2 position, const glm::vec4& color, std::string_view text);
		void AddImage(ImTextureRef texture, const Engine::PainterRect& rect, const Engine::PainterStyle& style, const Engine::PainterRect& uvs = Engine::PainterRect{ { 0.0f, 0.0f }, { 1.0f, 1.0f } });

		// Primitive groups, as Painter's. Keep ImGui from drawing between a group's shapes: the
		// group would be split over two segments, and each part would cast its own shadow and
		// glow. That is reported as a problem.
		void BeginGroup(const Engine::PainterEffects& effects);
		void EndGroup();

		// The recorded frame: all shapes of the window, in the viewport's framebuffer pixels
		const Engine::Painter& GetPainter() const { return Recording; }
		// The segments, in drawing order, each a run of the painter's commands
		const std::vector<Engine::PainterSegment>& GetSegments() const { return Segments; }
		// The viewport the segments are drawn into
		ImGuiID GetViewportId() const { return ViewportId; }

		// Misuse found while recording this frame, one line per kind. After End(), the painter's
		// own problems are included.
		const std::vector<std::string>& GetProblems() const { return Problems; }

		// What a segment's callback carries in the draw list (ImDrawCmd::UserCallbackData; ImGui
		// keeps a copy)
		struct SegmentCallbackData
		{
			const WindowPainter* Painter = nullptr;
			uint32_t Segment = 0;
		};

		// Draws one segment where ImGui calls its callback, in the command buffer ImGui renders
		// the viewport with. Set by the renderer.
		using SegmentDrawer = void(*)(const WindowPainter& painter, uint32_t segment, const ImDrawCmd& command);
		static void SetSegmentDrawer(SegmentDrawer drawer);

		// The callback each segment starts with in the draw list. It hands the segment to the
		// segment drawer; without one, nothing is drawn.
		static void DrawSegmentCallback(const ImDrawList* drawList, const ImDrawCmd* command);

	private:
		// Called before each shape: starts a new segment if this is the first shape, or if ImGui
		// has drawn or changed its clip rect since the current segment started. False outside
		// Begin()/End().
		bool PrepareSegment();
		void StartSegment(const ImVec4& clipRect);
		// Sets the current segment's command count from the commands recorded since it started
		void CloseSegment();

		void ReportProblem(const std::string& problem);

		Engine::Painter Recording;
		std::vector<Engine::PainterSegment> Segments;
		std::vector<std::string> Problems;

		ImDrawList* DrawList = nullptr;
		ImGuiID ViewportId = 0;
		bool bRecording = false;

		// The draw list's size right after the current segment's callbacks: if it has changed,
		// ImGui has drawn since.
		bool bSegmentOpen = false;
		int DrawListCommandsAtSegmentStart = 0;
		int DrawListIndicesAtSegmentStart = 0;
		// ImGui's clip rect of the current segment, in ImGui's screen coordinates
		ImVec4 SegmentClipRect;

		// How deep BeginGroup() calls are nested, and whether the open group has shapes yet
		uint32_t GroupDepth = 0;
		bool bGroupHasShapes = false;
	};
}

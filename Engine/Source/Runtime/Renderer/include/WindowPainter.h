#pragma once

#include "Painter.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::UI
{
	// Paints into an ImGui window, in order with ImGui's own drawing there: a shape painted after
	// an ImGui widget lands in front of it, and the next widget in front of the shape. The plan is
	// in Design/ui-painter.md, stage 2.
	//
	// Inside a window, ask for the window's own with GetWindowPainter(). It records like a
	// Painter, but in ImGui's screen coordinates, as ImDrawList does:
	//
	//     Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
	//     painter.AddRoundedRect(card, 6.0f, PainterStyle::Filled(cardColor)); // behind the text
	//     ImGui::TextUnformatted("Hello");
	//     painter.AddCircle(badgeCenter, 4.0f, PainterStyle::Filled(accent));  // in front of it
	//
	// How it keeps the order: the shapes are cut into segments (PainterSegment) wherever ImGui has
	// drawn in between, and each segment starts with a callback in the window's draw list. ImGui
	// renders a draw list in order, so the callback, where the renderer draws the segment, comes
	// after everything ImGui drew before the shapes and before everything it draws after them.
	//
	// Painting is clipped like ImGui's drawing: each shape gets ImGui's current clip rect, so
	// child windows, tables and scrolling regions clip it too. PushClipRect() pushes onto ImGui's
	// own clip rect stack. A new clip rect starts a new segment, so all shapes of a segment share
	// one clip rect.
	//
	// Shapes are recorded in the viewport's framebuffer pixels: ImGui positions minus the
	// viewport's position, times its framebuffer scale. Those are the pixels the segments are
	// drawn into.
	//
	// The draw list keeps a pointer to this window painter until ImGui renders it, so it must not
	// move or be destroyed before then. Like Painter, it never logs or asserts; misuse is collected
	// in GetProblems().
	//
	// A window painter can also paint behind everything already in its draw list (Behind), for a
	// window's card painted after the window is built (UIChrome.h).

	// Where a window painter's segments go in its draw list
	enum class EWindowPainterPlacement
	{
		// After what ImGui has drawn so far, in order with ImGui's drawing: the usual
		InOrder,
		// In front of everything in the draw list, behind all of ImGui's drawing there. Segments
		// follow each other in the order they were painted.
		Behind,
	};

	class WindowPainter
	{
	public:
		// Starts a frame of painting into `drawList`, a window's, which ImGui renders into `viewport`.
		// Drops the previous frame's shapes, segments and problems. `windowName` names the window
		// in problems and statistics.
		void Begin(ImDrawList& drawList, const ImGuiViewport& viewport, std::string_view windowName = {},
			EWindowPainterPlacement placement = EWindowPainterPlacement::InOrder);

		// Ends the frame: closes what was left open and adds the painter's problems to this one's
		void End();

		bool IsRecording() const { return bRecording; }

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
		void AddImageGlow(ImTextureRef texture, const Engine::PainterRect& rect, float reach, float strength, const Engine::PainterRect& uvs = Engine::PainterRect{ { 0.0f, 0.0f }, { 1.0f, 1.0f } });
		// The backdrop is as large as the viewport's framebuffer, which the segments are drawn into
		void AddGlass(ImTextureRef backdrop, const Engine::PainterRect& rect, float radius, const Engine::PainterStyle& style, const Engine::PainterGlass& glass = Engine::PainterGlass{});

		// Clip rects, as Painter's, but on ImGui's clip rect stack (ImDrawList::PushClipRect(), cut
		// by the current one), so whatever ImGui draws until PopClipRect() is clipped too. The rect
		// is in the current transform's units. Pop each before the window ends.
		void PushClipRect(const Engine::PainterRect& rect);
		void PopClipRect();

		// Transforms, as Painter's: scale, then move, applied to everything painted until
		// PopTransform(), and to clip rects pushed meanwhile. ImGui's own drawing isn't affected.
		void PushTransform(float scale, glm::vec2 offset);
		void PopTransform();

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
		const ImDrawList* GetDrawList() const { return DrawList; }
		const std::string& GetWindowName() const { return WindowName; }
		EWindowPainterPlacement GetPlacement() const { return Placement; }

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
		// the viewport with. Set by the renderer, with `userData` handed back on each call.
		using SegmentDrawer = void(*)(const WindowPainter& painter, uint32_t segment, const ImDrawCmd& command, void* userData);
		static void SetSegmentDrawer(SegmentDrawer drawer, void* userData);

		// The callback each segment starts with in the draw list. It hands the segment to the
		// segment drawer; without one, nothing is drawn.
		static void DrawSegmentCallback(const ImDrawList* drawList, const ImDrawCmd* command);

	private:
		// Called before each shape: starts a new segment if this is the first shape, or if ImGui
		// has drawn or changed its clip rect since the current segment started. False outside
		// Begin()/End().
		bool PrepareSegment();
		void StartSegment(const ImVec4& clipRect);
		// Puts a segment's callbacks in front of the draw list, after the Behind segments before it
		void InsertSegmentCallbacksInFront(const SegmentCallbackData& data, const ImVec4& clipRect);
		// Sets the current segment's command count from the commands recorded since it started
		void CloseSegment();

		void ReportProblem(const std::string& problem);

		Engine::Painter Recording;
		std::vector<Engine::PainterSegment> Segments;
		std::vector<std::string> Problems;

		ImDrawList* DrawList = nullptr;
		ImGuiID ViewportId = 0;
		std::string WindowName;
		EWindowPainterPlacement Placement = EWindowPainterPlacement::InOrder;
		bool bRecording = false;

		// From ImGui's screen coordinates to the viewport's framebuffer pixels
		Engine::PainterTransform ViewportTransform;
		// The transforms pushed with PushTransform(), in screen coordinates; never empty, the
		// bottom one changes nothing
		std::vector<Engine::PainterTransform> Transforms{ Engine::PainterTransform{} };
		// Clip rects pushed onto ImGui's stack with PushClipRect() and not popped yet
		uint32_t ClipRectDepth = 0;

		// The draw list's size, command buffer and channel right after the current segment's
		// callbacks: if one has changed, ImGui has drawn since, or moved on to another table column.
		bool bSegmentOpen = false;
		int DrawListCommandsAtSegmentStart = 0;
		int DrawListIndicesAtSegmentStart = 0;
		const ImDrawCmd* DrawListCommandDataAtSegmentStart = nullptr;
		int DrawListChannelAtSegmentStart = 0;
		// ImGui's clip rect of the current segment, in ImGui's screen coordinates
		ImVec4 SegmentClipRect;

		// How deep BeginGroup() calls are nested, and whether the open group has shapes yet
		uint32_t GroupDepth = 0;
		bool bGroupHasShapes = false;
	};

	// The window painter of the ImGui window being built (between ImGui::Begin() and End(), or
	// BeginChild() and EndChild()), started on the first call in a frame. The renderer collects
	// them after the UI is built (EndWindowPainters()) and draws their segments where ImGui
	// renders the windows.
	WindowPainter& GetWindowPainter();

	// A window painter over everything ImGui draws in `viewport` (the main one if null): it paints
	// into the viewport's foreground draw list, which ImGui renders after all windows. For
	// overlays; it takes no input. Started on the first call in a frame and gathered like the
	// windows' ones.
	WindowPainter& GetForegroundPainter(ImGuiViewport* viewport = nullptr);

	// The window painter of any draw list, e.g. a window's found by a pass over all windows once
	// the UI is built, outside its Begin()/End() (UIChrome.h). `placement` says where its shapes
	// go; there is one painter per draw list and placement. Started on the first call in a frame
	// and gathered like the windows' ones.
	WindowPainter& GetPainterFor(ImDrawList& drawList, const ImGuiViewport& viewport, std::string_view name,
		EWindowPainterPlacement placement = EWindowPainterPlacement::InOrder);

	// Ends every window painter started this frame and returns them, for the renderer to draw.
	// Call it after the UI is built and before ImGui::Render(). They stay valid until the next
	// frame's first GetWindowPainter().
	std::span<WindowPainter* const> EndWindowPainters();

	// One segment of a window painter
	struct WindowSegmentRef
	{
		const WindowPainter* Painter = nullptr;
		uint32_t Segment = 0;

		bool operator==(const WindowSegmentRef&) const = default;
	};

	// Segments that follow each other in a viewport's render order with nothing ImGui draws
	// between them, so they can be drawn as one (stage 3, change 8)
	struct WindowSegmentRun
	{
		std::vector<WindowSegmentRef> Segments;
	};

	// The runs of segments in a viewport's draw data, made by ImGui::Render(), in render order:
	// its draw lists one after the other, command by command. A run ends where ImGui draws: a
	// command with elements, or a callback other than a segment's or ImGui's render state reset.
	// Commands without elements and ImGui's resets come between segments without ending a run.
	// A run also ends before each draw list in `breakBefore`, e.g. where glass cuts the draw
	// data for its backdrop (UIChrome.h), so that no run is drawn on both sides of the cut.
	std::vector<WindowSegmentRun> FindWindowSegmentRuns(const ImDrawData& drawData, std::span<const ImDrawList* const> breakBefore = {});
}

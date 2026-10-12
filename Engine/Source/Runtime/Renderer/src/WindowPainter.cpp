#include "WindowPainter.h"

#include <imgui_internal.h> // ImGui::GetCurrentWindowRead(), for the window's name

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace Nyx::UI
{
	namespace
	{
		WindowPainter::SegmentDrawer CurrentSegmentDrawer = nullptr;
		void* CurrentSegmentDrawerData = nullptr;

		bool IsSameClipRect(const ImVec4& a, const ImVec4& b)
		{
			return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
		}

		// The window painters GetWindowPainter() hands out. They are reused from frame to frame,
		// and never move: the draw lists point at them until ImGui renders.
		struct WindowPainterPool
		{
			std::vector<std::unique_ptr<WindowPainter>> Painters;
			// This frame's, in the order the windows first painted
			std::vector<WindowPainter*> Used;

			// The frame Used belongs to
			ImGuiContext* Context = nullptr;
			int Frame = -1;
		};

		WindowPainterPool& GetWindowPainterPool()
		{
			static WindowPainterPool pool;
			return pool;
		}

		// Forgets the frame's window painters. Those nobody ended had no renderer to draw them;
		// their frame is ended now, quietly.
		void ForgetFrame(WindowPainterPool& pool)
		{
			for (WindowPainter* painter : pool.Used)
			{
				if (painter->IsRecording())
				{
					painter->End();
				}
			}
			pool.Used.clear();
		}

		// When the ImGui context shuts down, its draw lists go; so do the window painters' ties to them.
		void OnContextShutdown(ImGuiContext*, ImGuiContextHook*)
		{
			WindowPainterPool& pool = GetWindowPainterPool();
			ForgetFrame(pool);
			pool.Context = nullptr;
			pool.Frame = -1;
		}

		// On the first call in a new frame, or with another ImGui context, forgets the last frame's
		// window painters.
		void StartFrameIfNew(WindowPainterPool& pool)
		{
			ImGuiContext* context = ImGui::GetCurrentContext();
			const int frame = ImGui::GetFrameCount();
			if (pool.Context == context && pool.Frame == frame)
			{
				return;
			}

			ForgetFrame(pool);
			if (pool.Context != context)
			{
				// A later context may get this one's address; forgetting at shutdown keeps them apart.
				ImGuiContextHook hook;
				hook.Type = ImGuiContextHookType_Shutdown;
				hook.Callback = &OnContextShutdown;
				ImGui::AddContextHook(context, &hook);
			}
			pool.Context = context;
			pool.Frame = frame;
		}
	}

	void WindowPainter::Begin(ImDrawList& drawList, const ImGuiViewport& viewport, std::string_view windowName, EWindowPainterPlacement placement)
	{
		Problems.clear();
		if (bRecording)
		{
			ReportProblem("Begin() without an End() for the previous frame");
		}

		DrawList = &drawList;
		ViewportId = viewport.ID;
		WindowName = windowName;
		Placement = placement;
		Segments.clear();
		bSegmentOpen = false;
		Transforms.assign(1, Engine::PainterTransform{});
		ClipRectDepth = 0;
		GroupDepth = 0;
		bGroupHasShapes = false;
		bRecording = true;

		// ImGui's backends scale both axes alike; so does the painter's transform.
		const float scale = viewport.FramebufferScale.x > 0.0f ? viewport.FramebufferScale.x : 1.0f;
		const uint32_t targetWidth = static_cast<uint32_t>(std::lround(std::max(viewport.Size.x, 0.0f) * scale));
		const uint32_t targetHeight = static_cast<uint32_t>(std::lround(std::max(viewport.Size.y, 0.0f) * scale));
		Recording.BeginFrame(targetWidth, targetHeight);

		// (position - viewport position) * scale = position * scale - viewport position * scale
		ViewportTransform = Engine::PainterTransform{ scale, glm::vec2(-viewport.Pos.x, -viewport.Pos.y) * scale };
		Recording.PushTransform(ViewportTransform.Scale, ViewportTransform.Offset);
	}

	void WindowPainter::End()
	{
		if (!bRecording)
		{
			ReportProblem("End() without a Begin()");
			return;
		}

		CloseSegment();
		if (ClipRectDepth > 0)
		{
			// ImGui's stack is left as it is: popping now could pop what the window pushed since.
			ReportProblem("PushClipRect() without a PopClipRect() by the end of the frame");
		}
		if (Transforms.size() > 1)
		{
			ReportProblem("PushTransform() without a PopTransform() by the end of the frame");
			for (size_t index = 1; index < Transforms.size(); ++index)
			{
				Recording.PopTransform();
			}
			Transforms.resize(1);
		}
		if (!Segments.empty())
		{
			Recording.PopClipRect();
		}
		Recording.PopTransform();
		// Reports a group left open
		Recording.EndFrame();

		for (const std::string& problem : Recording.GetProblems())
		{
			ReportProblem(problem);
		}
		bRecording = false;
	}

	void WindowPainter::AddRect(const Engine::PainterRect& rect, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddRect(rect, style);
		}
	}

	void WindowPainter::AddRoundedRect(const Engine::PainterRect& rect, float radius, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddRoundedRect(rect, radius, style);
		}
	}

	void WindowPainter::AddRoundedRect(const Engine::PainterRect& rect, const Engine::PainterCornerRadii& radii, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddRoundedRect(rect, radii, style);
		}
	}

	void WindowPainter::AddCircle(glm::vec2 center, float radius, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddCircle(center, radius, style);
		}
	}

	void WindowPainter::AddCircleSector(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddCircleSector(center, innerRadius, outerRadius, startAngle, endAngle, style);
		}
	}

	void WindowPainter::AddArc(glm::vec2 center, float radius, float width, float startAngle, float endAngle, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddArc(center, radius, width, startAngle, endAngle, style);
		}
	}

	void WindowPainter::AddLine(glm::vec2 from, glm::vec2 to, float width, Engine::EPainterLineCap cap, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddLine(from, to, width, cap, style);
		}
	}

	void WindowPainter::AddTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Engine::PainterStyle& style)
	{
		if (PrepareSegment())
		{
			Recording.AddTriangle(a, b, c, style);
		}
	}

	void WindowPainter::AddText(ImFont* font, float size, glm::vec2 position, const glm::vec4& color, std::string_view text)
	{
		// ImGui can't draw in the middle of one text, so all its glyphs share a segment.
		if (PrepareSegment())
		{
			Recording.AddText(font, size, position, color, text);
		}
	}

	void WindowPainter::AddImage(ImTextureRef texture, const Engine::PainterRect& rect, const Engine::PainterStyle& style, const Engine::PainterRect& uvs)
	{
		if (PrepareSegment())
		{
			Recording.AddImage(texture, rect, style, uvs);
		}
	}

	void WindowPainter::AddImageGlow(ImTextureRef texture, const Engine::PainterRect& rect, float reach, float strength, const Engine::PainterRect& uvs)
	{
		if (PrepareSegment())
		{
			Recording.AddImageGlow(texture, rect, reach, strength, uvs);
		}
	}

	void WindowPainter::AddGlass(ImTextureRef backdrop, const Engine::PainterRect& rect, float radius, const Engine::PainterStyle& style, const Engine::PainterGlass& glass)
	{
		if (PrepareSegment())
		{
			Recording.AddGlass(backdrop, rect, radius, style, glass);
		}
	}

	void WindowPainter::PushClipRect(const Engine::PainterRect& rect)
	{
		if (!bRecording)
		{
			ReportProblem("PushClipRect() outside Begin()/End(); it is ignored");
			return;
		}
		const Engine::PainterRect onScreen = Transforms.back().Apply(rect);
		const glm::vec2 min = glm::min(onScreen.Min, onScreen.Max);
		const glm::vec2 max = glm::max(onScreen.Min, onScreen.Max);
		DrawList->PushClipRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), true);
		++ClipRectDepth;
	}

	void WindowPainter::PopClipRect()
	{
		if (!bRecording || ClipRectDepth == 0)
		{
			ReportProblem("PopClipRect() without a PushClipRect()");
			return;
		}
		DrawList->PopClipRect();
		--ClipRectDepth;
	}

	void WindowPainter::PushTransform(float scale, glm::vec2 offset)
	{
		if (!bRecording)
		{
			ReportProblem("PushTransform() outside Begin()/End(); it is ignored");
			return;
		}

		// Painter reports a scale of 0 or less and pushes the outer transform again; so does this.
		const Engine::PainterTransform& outer = Transforms.back();
		Transforms.push_back(scale > 0.0f ? Engine::PainterTransform{ outer.Scale * scale, outer.Apply(offset) } : outer);
		Recording.PushTransform(scale, offset);
	}

	void WindowPainter::PopTransform()
	{
		// The painter's bottom transform is the viewport's, which only End() pops.
		if (!bRecording || Transforms.size() <= 1)
		{
			ReportProblem("PopTransform() without a PushTransform()");
			return;
		}
		Transforms.pop_back();
		Recording.PopTransform();
	}

	void WindowPainter::BeginGroup(const Engine::PainterEffects& effects)
	{
		if (GroupDepth == 0)
		{
			bGroupHasShapes = false;
		}
		++GroupDepth;
		Recording.BeginGroup(effects);
	}

	void WindowPainter::EndGroup()
	{
		if (GroupDepth > 0)
		{
			--GroupDepth;
		}
		Recording.EndGroup();
	}

	void WindowPainter::SetSegmentDrawer(SegmentDrawer drawer, void* userData)
	{
		CurrentSegmentDrawer = drawer;
		CurrentSegmentDrawerData = userData;
	}

	void WindowPainter::DrawSegmentCallback(const ImDrawList*, const ImDrawCmd* command)
	{
		if (CurrentSegmentDrawer == nullptr || command == nullptr || command->UserCallbackData == nullptr)
		{
			return;
		}

		// ImGui keeps the data in a byte buffer, so it may not be aligned for a pointer.
		SegmentCallbackData data;
		std::memcpy(&data, command->UserCallbackData, sizeof(data));
		if (data.Painter != nullptr)
		{
			CurrentSegmentDrawer(*data.Painter, data.Segment, *command, CurrentSegmentDrawerData);
		}
	}

	// The pool's window painter for the draw list and placement, started this frame if it isn't
	// yet: so one per window (child windows too), one per viewport's foreground, and one behind
	// each window that has a card
	WindowPainter& GetPainterFor(ImDrawList& drawList, const ImGuiViewport& viewport, std::string_view name, EWindowPainterPlacement placement)
	{
		WindowPainterPool& pool = GetWindowPainterPool();
		StartFrameIfNew(pool);

		for (WindowPainter* painter : pool.Used)
		{
			if (painter->GetDrawList() == &drawList && painter->GetPlacement() == placement)
			{
				return *painter;
			}
		}

		if (pool.Used.size() == pool.Painters.size())
		{
			pool.Painters.push_back(std::make_unique<WindowPainter>());
		}
		WindowPainter& painter = *pool.Painters[pool.Used.size()];
		pool.Used.push_back(&painter);
		painter.Begin(drawList, viewport, name, placement);
		return painter;
	}

	WindowPainter& GetWindowPainter()
	{
		return GetPainterFor(*ImGui::GetWindowDrawList(), *ImGui::GetWindowViewport(), ImGui::GetCurrentWindowRead()->Name);
	}

	WindowPainter& GetForegroundPainter(ImGuiViewport* viewport)
	{
		ImGuiViewport* target = viewport != nullptr ? viewport : ImGui::GetMainViewport();
		return GetPainterFor(*ImGui::GetForegroundDrawList(target), *target, "Foreground");
	}

	std::span<WindowPainter* const> EndWindowPainters()
	{
		WindowPainterPool& pool = GetWindowPainterPool();
		StartFrameIfNew(pool);

		for (WindowPainter* painter : pool.Used)
		{
			if (painter->IsRecording())
			{
				painter->End();
			}
		}
		return pool.Used;
	}

	std::vector<WindowSegmentRun> FindWindowSegmentRuns(const ImDrawData& drawData, std::span<const ImDrawList* const> breakBefore)
	{
		std::vector<WindowSegmentRun> runs;
		bool bRunOpen = false;
		for (const ImDrawList* drawList : drawData.CmdLists)
		{
			if (std::find(breakBefore.begin(), breakBefore.end(), drawList) != breakBefore.end())
			{
				bRunOpen = false;
			}
			for (const ImDrawCmd& command : drawList->CmdBuffer)
			{
				if (command.UserCallback == &WindowPainter::DrawSegmentCallback && command.UserCallbackData != nullptr)
				{
					WindowPainter::SegmentCallbackData data;
					std::memcpy(&data, command.UserCallbackData, sizeof(data));
					if (!bRunOpen)
					{
						runs.emplace_back();
						bRunOpen = true;
					}
					runs.back().Segments.push_back(WindowSegmentRef{ data.Painter, data.Segment });
				}
				else if (command.UserCallback == ImDrawCallback_ResetRenderState || (command.UserCallback == nullptr && command.ElemCount == 0))
				{
					// Draws nothing
				}
				else
				{
					bRunOpen = false;
				}
			}
		}
		return runs;
	}

	bool WindowPainter::PrepareSegment()
	{
		if (!bRecording)
		{
			ReportProblem("A shape painted outside Begin()/End(); it is dropped");
			return false;
		}

		const ImVec2 clipMin = DrawList->GetClipRectMin();
		const ImVec2 clipMax = DrawList->GetClipRectMax();
		const ImVec4 clipRect(clipMin.x, clipMin.y, clipMax.x, clipMax.y);
		// Tables draw each column into a channel of its own (ImDrawListSplitter), swapped into
		// the draw list's buffers; another channel needs a segment of its own too. Tables switch
		// channels with a splitter of their own, not the draw list's, so a switch shows as other
		// buffers: the command buffer's memory changes. Behind everything, nothing ImGui draws
		// comes between.
		const bool bImGuiHasDrawn = Placement == EWindowPainterPlacement::InOrder &&
			(DrawList->CmdBuffer.Size != DrawListCommandsAtSegmentStart || DrawList->IdxBuffer.Size != DrawListIndicesAtSegmentStart ||
				DrawList->CmdBuffer.Data != DrawListCommandDataAtSegmentStart || DrawList->_Splitter._Current != DrawListChannelAtSegmentStart);
		if (!bSegmentOpen || bImGuiHasDrawn || !IsSameClipRect(clipRect, SegmentClipRect))
		{
			StartSegment(clipRect);
		}

		if (GroupDepth > 0)
		{
			bGroupHasShapes = true;
		}
		return true;
	}

	void WindowPainter::StartSegment(const ImVec4& clipRect)
	{
		CloseSegment();

		if (GroupDepth > 0 && bGroupHasShapes)
		{
			ReportProblem("ImGui drew between the shapes of one group (BeginGroup()/EndGroup()); each part casts its own shadow and glow");
		}

		// Shapes from here on get ImGui's clip rect, cut by the viewport, in framebuffer pixels.
		// Transforms pushed with PushTransform() don't apply: ImGui's clip rect is on the screen.
		if (!Segments.empty())
		{
			Recording.PopClipRect();
		}
		Recording.PushTargetClipRect(ViewportTransform.Apply(Engine::PainterRect{ { clipRect.x, clipRect.y }, { clipRect.z, clipRect.w } }));

		const uint32_t segmentIndex = static_cast<uint32_t>(Segments.size());
		Segments.push_back(Engine::PainterSegment{ static_cast<uint32_t>(Recording.GetCommands().size()), 0 });

		// The segment is drawn here, then ImGui sets up its own render state again for what it
		// draws next.
		SegmentCallbackData data{ this, segmentIndex };
		if (Placement == EWindowPainterPlacement::Behind)
		{
			InsertSegmentCallbacksInFront(data, clipRect);
		}
		else
		{
			DrawList->AddCallback(&WindowPainter::DrawSegmentCallback, &data, sizeof(data));
			DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}

		bSegmentOpen = true;
		DrawListCommandsAtSegmentStart = DrawList->CmdBuffer.Size;
		DrawListIndicesAtSegmentStart = DrawList->IdxBuffer.Size;
		DrawListCommandDataAtSegmentStart = DrawList->CmdBuffer.Data;
		DrawListChannelAtSegmentStart = DrawList->_Splitter._Current;
		SegmentClipRect = clipRect;
	}

	void WindowPainter::InsertSegmentCallbacksInFront(const SegmentCallbackData& data, const ImVec4& clipRect)
	{
		// What ImDrawList::AddCallback() makes, put in by hand: the draw list only appends. The
		// data goes into the draw list's callback data, which ImGui hands to the callback when it
		// renders. Both commands draw no triangles, so their offsets only keep the list tidy.
		ImDrawCmd segment;
		segment.ClipRect = clipRect;
		segment.TexRef = DrawList->_CmdHeader.TexRef;
		segment.UserCallback = &WindowPainter::DrawSegmentCallback;
		segment.UserCallbackDataSize = static_cast<int>(sizeof(data));
		segment.UserCallbackDataOffset = DrawList->_CallbacksDataBuf.Size;
		DrawList->_CallbacksDataBuf.resize(DrawList->_CallbacksDataBuf.Size + static_cast<int>(sizeof(data)));
		std::memcpy(DrawList->_CallbacksDataBuf.Data + segment.UserCallbackDataOffset, &data, sizeof(data));

		ImDrawCmd reset = segment;
		reset.UserCallback = ImDrawCallback_ResetRenderState;
		reset.UserCallbackDataSize = 0;
		reset.UserCallbackDataOffset = -1;

		// After the Behind segments already in front, two commands each
		const int index = std::min(static_cast<int>(data.Segment) * 2, DrawList->CmdBuffer.Size);
		if (index < DrawList->CmdBuffer.Size)
		{
			segment.VtxOffset = reset.VtxOffset = DrawList->CmdBuffer[index].VtxOffset;
			segment.IdxOffset = reset.IdxOffset = DrawList->CmdBuffer[index].IdxOffset;
		}
		DrawList->CmdBuffer.insert(DrawList->CmdBuffer.Data + index, reset);
		DrawList->CmdBuffer.insert(DrawList->CmdBuffer.Data + index, segment);
	}

	void WindowPainter::CloseSegment()
	{
		if (!bSegmentOpen)
		{
			return;
		}
		Engine::PainterSegment& segment = Segments.back();
		segment.CommandCount = static_cast<uint32_t>(Recording.GetCommands().size()) - segment.FirstCommand;
		bSegmentOpen = false;
	}

	void WindowPainter::ReportProblem(const std::string& problem)
	{
		if (std::find(Problems.begin(), Problems.end(), problem) == Problems.end())
		{
			Problems.push_back(problem);
		}
	}
}

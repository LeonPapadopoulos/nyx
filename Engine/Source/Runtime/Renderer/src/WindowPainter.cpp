#include "WindowPainter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nyx::UI
{
	namespace
	{
		WindowPainter::SegmentDrawer CurrentSegmentDrawer = nullptr;

		bool IsSameClipRect(const ImVec4& a, const ImVec4& b)
		{
			return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
		}
	}

	void WindowPainter::Begin(ImDrawList& drawList, const ImGuiViewport& viewport)
	{
		Problems.clear();
		if (bRecording)
		{
			ReportProblem("Begin() without an End() for the previous frame");
		}

		DrawList = &drawList;
		ViewportId = viewport.ID;
		Segments.clear();
		bSegmentOpen = false;
		GroupDepth = 0;
		bGroupHasShapes = false;
		bRecording = true;

		// ImGui's backends scale both axes alike; so does the painter's transform.
		const float scale = viewport.FramebufferScale.x > 0.0f ? viewport.FramebufferScale.x : 1.0f;
		const uint32_t targetWidth = static_cast<uint32_t>(std::lround(std::max(viewport.Size.x, 0.0f) * scale));
		const uint32_t targetHeight = static_cast<uint32_t>(std::lround(std::max(viewport.Size.y, 0.0f) * scale));
		Recording.BeginFrame(targetWidth, targetHeight);

		// (position - viewport position) * scale = position * scale - viewport position * scale
		Recording.PushTransform(scale, glm::vec2(-viewport.Pos.x, -viewport.Pos.y) * scale);
	}

	void WindowPainter::End()
	{
		if (!bRecording)
		{
			ReportProblem("End() without a Begin()");
			return;
		}

		CloseSegment();
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

	void WindowPainter::SetSegmentDrawer(SegmentDrawer drawer)
	{
		CurrentSegmentDrawer = drawer;
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
			CurrentSegmentDrawer(*data.Painter, data.Segment, *command);
		}
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
		const bool bImGuiHasDrawn = DrawList->CmdBuffer.Size != DrawListCommandsAtSegmentStart || DrawList->IdxBuffer.Size != DrawListIndicesAtSegmentStart;
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

		// Shapes from here on get ImGui's clip rect, cut by the viewport. It goes through the
		// painter's transform like the shapes do, into framebuffer pixels.
		if (!Segments.empty())
		{
			Recording.PopClipRect();
		}
		Recording.PushClipRect(Engine::PainterRect{ { clipRect.x, clipRect.y }, { clipRect.z, clipRect.w } });

		const uint32_t segmentIndex = static_cast<uint32_t>(Segments.size());
		Segments.push_back(Engine::PainterSegment{ static_cast<uint32_t>(Recording.GetCommands().size()), 0 });

		// The segment is drawn here, then ImGui sets up its own render state again for what it
		// draws next.
		SegmentCallbackData data{ this, segmentIndex };
		DrawList->AddCallback(&WindowPainter::DrawSegmentCallback, &data, sizeof(data));
		DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

		bSegmentOpen = true;
		DrawListCommandsAtSegmentStart = DrawList->CmdBuffer.Size;
		DrawListIndicesAtSegmentStart = DrawList->IdxBuffer.Size;
		SegmentClipRect = clipRect;
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

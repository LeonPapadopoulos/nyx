#include "UIDiagnostics.h"

#include "WindowPainter.h"

#include <imgui_internal.h> // ImGui's windows and their draw lists

#include <algorithm>
#include <cfloat>

namespace Nyx::UI
{
	using Engine::PainterRect;
	using Engine::PainterStyle;

	namespace
	{
		// A debugging aid, not part of the look, so its colours aren't the theme's
		const glm::vec4 MarkOutline{ 1.0f, 0.15f, 0.2f, 0.95f };
		const glm::vec4 MarkFill{ 1.0f, 0.15f, 0.2f, 0.18f };
		// Past this many shapes a frame, the rest go unmarked: the marks themselves are drawn
		constexpr uint32_t MaxMarks = 4000;

		struct DiagnosticsState
		{
			bool bMarkImGuiShapes = false;
			uint32_t ShapeCount = 0;
			uint32_t WindowCount = 0;
		};

		DiagnosticsState& GetState()
		{
			static DiagnosticsState state;
			return state;
		}

		// Where in the font atlas ImGui's shapes read: its white pixel, and its baked lines
		struct ShapeTexels
		{
			ImVec2 WhitePixel;
			ImVec2 LinesMin;
			ImVec2 LinesMax;

			bool Contains(const ImVec2& uv) const
			{
				return (uv.x == WhitePixel.x && uv.y == WhitePixel.y) ||
					(uv.x >= LinesMin.x && uv.x <= LinesMax.x && uv.y >= LinesMin.y && uv.y <= LinesMax.y);
			}
		};

		ShapeTexels GetShapeTexels(const ImFontAtlas& atlas)
		{
			ShapeTexels texels;
			texels.WhitePixel = atlas.TexUvWhitePixel;
			texels.LinesMin = ImVec2(FLT_MAX, FLT_MAX);
			texels.LinesMax = ImVec2(-FLT_MAX, -FLT_MAX);
			for (const ImVec4& line : atlas.TexUvLines)
			{
				texels.LinesMin = ImVec2(std::min({ texels.LinesMin.x, line.x, line.z }), std::min({ texels.LinesMin.y, line.y, line.w }));
				texels.LinesMax = ImVec2(std::max({ texels.LinesMax.x, line.x, line.z }), std::max({ texels.LinesMax.y, line.y, line.w }));
			}
			return texels;
		}

		// Marks the shapes of one draw list; returns how many
		uint32_t MarkShapesOf(const ImDrawList& drawList, ImGuiViewport& viewport, const ImFontAtlas& atlas, const ShapeTexels& texels, uint32_t room)
		{
			uint32_t marked = 0;
			WindowPainter* marks = nullptr;
			const auto mark = [&](ImRect shape, const ImVec4& clipRect)
			{
				// ImGui clips it when it renders, so the mark is clipped the same.
				shape.ClipWith(ImRect(clipRect.x, clipRect.y, clipRect.z, clipRect.w));
				if (shape.GetWidth() <= 0.0f || shape.GetHeight() <= 0.0f || marked >= room)
				{
					return;
				}
				if (marks == nullptr)
				{
					marks = &GetForegroundPainter(&viewport);
				}
				const PainterRect rect{ { shape.Min.x, shape.Min.y }, { shape.Max.x, shape.Max.y } };
				marks->AddRect(rect, PainterStyle::Filled(MarkFill));
				marks->AddRect(rect, PainterStyle::Outlined(MarkOutline, 1.0f));
				++marked;
			};

			for (const ImDrawCmd& command : drawList.CmdBuffer)
			{
				// Shapes are on the font atlas; images and painter segments aren't.
				if (command.UserCallback != nullptr || command.ElemCount == 0 || command.TexRef._TexData != atlas.TexData)
				{
					continue;
				}

				// Triangles of one primitive use its own vertices, which come after the previous
				// primitive's: a triangle using none of the current shape's vertices starts the next.
				bool bShapeOpen = false;
				ImRect shape;
				unsigned int shapeLastVertex = 0;
				for (unsigned int index = command.IdxOffset; index + 2 < command.IdxOffset + command.ElemCount; index += 3)
				{
					const unsigned int vertices[3] = { command.VtxOffset + drawList.IdxBuffer[index], command.VtxOffset + drawList.IdxBuffer[index + 1],
						command.VtxOffset + drawList.IdxBuffer[index + 2] };
					const ImDrawVert& a = drawList.VtxBuffer[vertices[0]];
					const ImDrawVert& b = drawList.VtxBuffer[vertices[1]];
					const ImDrawVert& c = drawList.VtxBuffer[vertices[2]];
					const bool bShape = texels.Contains(a.uv) && texels.Contains(b.uv) && texels.Contains(c.uv);
					const unsigned int firstVertex = std::min({ vertices[0], vertices[1], vertices[2] });
					if (bShapeOpen && (!bShape || firstVertex > shapeLastVertex))
					{
						mark(shape, command.ClipRect);
						bShapeOpen = false;
					}
					if (!bShape)
					{
						continue;
					}
					if (!bShapeOpen)
					{
						shape = ImRect(a.pos, a.pos);
						shapeLastVertex = 0;
						bShapeOpen = true;
					}
					shape.Add(a.pos);
					shape.Add(b.pos);
					shape.Add(c.pos);
					shapeLastVertex = std::max({ shapeLastVertex, vertices[0], vertices[1], vertices[2] });
				}
				if (bShapeOpen)
				{
					mark(shape, command.ClipRect);
				}
			}
			return marked;
		}
	}

	void SetMarkImGuiShapes(bool bEnabled)
	{
		DiagnosticsState& state = GetState();
		state.bMarkImGuiShapes = bEnabled;
		state.ShapeCount = 0;
		state.WindowCount = 0;
	}

	bool IsMarkingImGuiShapes()
	{
		return GetState().bMarkImGuiShapes;
	}

	void MarkImGuiShapes()
	{
		DiagnosticsState& state = GetState();
		state.ShapeCount = 0;
		state.WindowCount = 0;
		if (!state.bMarkImGuiShapes || ImGui::GetCurrentContext() == nullptr)
		{
			return;
		}

		ImGuiContext& g = *GImGui;
		const ImFontAtlas& atlas = *g.IO.Fonts;
		const ShapeTexels texels = GetShapeTexels(atlas);
		for (ImGuiWindow* window : g.Windows)
		{
			if (!window->Active || window->Hidden || window->Viewport == nullptr || window->DrawList == nullptr)
			{
				continue;
			}
			const uint32_t marked = MarkShapesOf(*window->DrawList, *window->Viewport, atlas, texels, MaxMarks - std::min(state.ShapeCount, MaxMarks));
			state.ShapeCount += marked;
			state.WindowCount += marked > 0 ? 1 : 0;
		}
	}

	uint32_t GetImGuiShapeCount()
	{
		return GetState().ShapeCount;
	}

	uint32_t GetImGuiShapeWindowCount()
	{
		return GetState().WindowCount;
	}
}

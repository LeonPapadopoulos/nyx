#include "UIDiagnostics.h"

#include "WindowPainter.h"

#include <imgui_internal.h> // ImGui's windows and their draw lists

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

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
			EPainterHeatmap Heatmap = EPainterHeatmap::Off;
		};

		// The heatmap over the editor is see-through, so what lies under it still shows
		constexpr float HeatAlpha = 0.55f;

		// The canvas heatmap's colours (Heatmap() in Painter.frag): blue at 0, through green, to
		// red at 1
		ImU32 HeatColor(float amount, float alpha)
		{
			const glm::vec3 few{ 0.1f, 0.3f, 1.0f };
			const glm::vec3 some{ 0.1f, 0.9f, 0.2f };
			const glm::vec3 many{ 1.0f, 0.15f, 0.1f };
			const float t = std::clamp(amount, 0.0f, 1.0f);
			const glm::vec3 rgb = t < 0.5f ? glm::mix(few, some, t * 2.0f) : glm::mix(some, many, t * 2.0f - 1.0f);
			return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.r, rgb.g, rgb.b, alpha));
		}

		// Where a tile's value lies between blue and red: a few commands are nearly blue, one draw is blue
		float HeatAmount(EPainterHeatmap heatmap, uint32_t value)
		{
			return heatmap == EPainterHeatmap::Commands ? static_cast<float>(value) / static_cast<float>(PainterHeatmapCommandsRed)
														 : static_cast<float>(value - 1) / static_cast<float>(PainterHeatmapDrawsRed - 1);
		}

		// Text on a dark box, over everything in the draw list
		void AddLabel(ImDrawList& drawList, ImVec2 position, const char* text)
		{
			const ImVec2 size = ImGui::CalcTextSize(text);
			const ImVec2 padding(5.0f, 3.0f);
			drawList.AddRectFilled(position, ImVec2(position.x + size.x + padding.x * 2.0f, position.y + size.y + padding.y * 2.0f), IM_COL32(0, 0, 0, 200), 4.0f);
			drawList.AddText(ImVec2(position.x + padding.x, position.y + padding.y), IM_COL32(255, 255, 255, 255), text);
		}

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
					// Triangles ImGui draws fully transparent don't show, e.g. the background the
					// chrome takes back in an OS window of its own (UIChrome.h).
					const bool bVisible = ((a.col | b.col | c.col) & IM_COL32_A_MASK) != 0;
					const bool bShape = bVisible && texels.Contains(a.uv) && texels.Contains(b.uv) && texels.Contains(c.uv);
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

		// Each viewport's background and foreground draw lists, behind and over its windows, when
		// used this frame: e.g. the main window's focus border. They count as a window each.
		for (ImGuiViewportP* viewport : g.Viewports)
		{
			for (int list = 0; list < 2; ++list)
			{
				if (viewport->BgFgDrawLists[list] == nullptr || viewport->BgFgDrawListsLastFrame[list] != g.FrameCount)
				{
					continue;
				}
				const uint32_t marked = MarkShapesOf(*viewport->BgFgDrawLists[list], *viewport, atlas, texels, MaxMarks - std::min(state.ShapeCount, MaxMarks));
				state.ShapeCount += marked;
				state.WindowCount += marked > 0 ? 1 : 0;
			}
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

	void SetPainterHeatmap(EPainterHeatmap heatmap)
	{
		GetState().Heatmap = heatmap;
	}

	EPainterHeatmap GetPainterHeatmap()
	{
		return GetState().Heatmap;
	}

	void ShowPainterHeatmap(const Engine::PainterWindowStats& stats)
	{
		const EPainterHeatmap heatmap = GetState().Heatmap;
		if (heatmap == EPainterHeatmap::Off || ImGui::GetCurrentContext() == nullptr)
		{
			return;
		}

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		for (const Engine::PainterWindowStats::OsWindow& osWindow : stats.OsWindows)
		{
			const Engine::PainterHeatGrid& heat = osWindow.Heat;
			ImGuiViewport* viewport = ImGui::FindViewportByID(osWindow.ViewportId);
			if (viewport == nullptr || heat.TilesX == 0 || heat.TilesY == 0)
			{
				continue;
			}
			ImDrawList& drawList = *ImGui::GetForegroundDrawList(viewport);
			const std::vector<uint32_t>& values = heatmap == EPainterHeatmap::Commands ? heat.Commands : heat.Draws;
			const float tile = static_cast<float>(Engine::PainterTileSize) / osWindow.PixelsPerPoint;
			const ImVec2 origin = viewport->Pos;

			// Tiles side by side with the same value are one rect: few rects for the large areas
			// cards and the backdrop cover.
			for (uint32_t tileY = 0; tileY < heat.TilesY; ++tileY)
			{
				const uint32_t* row = values.data() + static_cast<size_t>(tileY) * heat.TilesX;
				for (uint32_t first = 0; first < heat.TilesX;)
				{
					uint32_t end = first + 1;
					while (end < heat.TilesX && row[end] == row[first])
					{
						++end;
					}
					if (row[first] > 0)
					{
						drawList.AddRectFilled(ImVec2(origin.x + static_cast<float>(first) * tile, origin.y + static_cast<float>(tileY) * tile),
							ImVec2(origin.x + static_cast<float>(end) * tile, origin.y + static_cast<float>(tileY + 1) * tile), HeatColor(HeatAmount(heatmap, row[first]), HeatAlpha));
					}
					first = end;
				}
			}

			// The legend, in the top right corner below the titlebar: the scale's colours from one to red
			const bool bCommands = heatmap == EPainterHeatmap::Commands;
			const uint32_t red = bCommands ? PainterHeatmapCommandsRed : PainterHeatmapDrawsRed;
			char legend[64];
			std::snprintf(legend, sizeof(legend), "Painter heatmap: %s per tile, 1 to %u+", bCommands ? "commands" : "draws", red);
			const ImVec2 legendSize = ImGui::CalcTextSize(legend);
			const ImVec2 legendAt(origin.x + viewport->Size.x - legendSize.x - 24.0f, origin.y + 40.0f);
			AddLabel(drawList, legendAt, legend);
			const float swatchWidth = (legendSize.x + 10.0f) / 8.0f;
			const float swatchTop = legendAt.y + legendSize.y + 8.0f;
			for (int swatch = 0; swatch < 8; ++swatch)
			{
				const float left = legendAt.x + static_cast<float>(swatch) * swatchWidth;
				drawList.AddRectFilled(ImVec2(left, swatchTop), ImVec2(left + swatchWidth, swatchTop + 8.0f), HeatColor(static_cast<float>(swatch) / 7.0f, 1.0f));
			}

			// The tile under the mouse, if it is over this OS window
			const float mouseX = (mouse.x - origin.x) / tile;
			const float mouseY = (mouse.y - origin.y) / tile;
			if (mouseX >= 0.0f && mouseY >= 0.0f && mouseX < static_cast<float>(heat.TilesX) && mouseY < static_cast<float>(heat.TilesY) &&
				GImGui->MouseViewport == viewport)
			{
				const size_t index = static_cast<size_t>(mouseY) * heat.TilesX + static_cast<size_t>(mouseX);
				char counts[64];
				std::snprintf(counts, sizeof(counts), "%u commands in %u draws", heat.Commands[index], heat.Draws[index]);
				AddLabel(drawList, ImVec2(mouse.x + 18.0f, mouse.y + 18.0f), counts);
			}
		}
	}
}

#include "PainterTestWindow.h"

#include "ImGuiSource.h"
#include "PainterDrawOptions.h"
#include "PainterTestSheet.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Nyx::Editor
{
	using namespace Nyx::Engine;

	namespace
	{
		constexpr float MinZoom = 0.25f;
		constexpr float MaxZoom = 8.0f;
		const glm::vec2 HomePan{ 16.0f, 16.0f };

		ImVec2 ToImVec2(glm::vec2 point, ImVec2 origin)
		{
			return ImVec2(origin.x + point.x, origin.y + point.y);
		}

		// The painter stores premultiplied colour; ImGui takes straight colour.
		ImU32 ToImGuiColor(uint32_t premultiplied)
		{
			const glm::vec4 color = UnpackPremultipliedColor(premultiplied);
			const glm::vec3 straight = color.a > 0.0f ? glm::vec3(color) / color.a : glm::vec3(0.0f);
			return ImGui::GetColorU32(ImVec4(straight.r, straight.g, straight.b, color.a));
		}

		// Whether ImGui replaced the texture during this frame: when its font atlas grows, ImGui
		// moves to a bigger texture and destroys the old one next frame. Glyphs recorded before
		// the growth still use the old texture, and the painter draws them from it this frame, but
		// ImGui asserts if a draw list is given it now (ImDrawList::PushTexture()).
		bool IsReplacedThisFrame(const ImTextureRef& texture)
		{
			return texture._TexData != nullptr && texture._TexData->WantDestroyNextFrame;
		}

		// ImDrawList rounds all chosen corners by one radius: the largest of the rect's.
		void DrawRectWithImDrawList(ImDrawList& drawList, const PainterCommand& command, ImVec2 origin, ImU32 color)
		{
			const auto payload = GetPayload<PainterRectPayload>(command);
			const PainterCornerRadii& radii = payload.Radii;
			const float rounding = std::max({ radii.TopLeft, radii.TopRight, radii.BottomRight, radii.BottomLeft });
			ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
			if (rounding > 0.0f)
			{
				corners = (radii.TopLeft > 0.0f ? ImDrawFlags_RoundCornersTopLeft : 0) | (radii.TopRight > 0.0f ? ImDrawFlags_RoundCornersTopRight : 0) |
					(radii.BottomRight > 0.0f ? ImDrawFlags_RoundCornersBottomRight : 0) | (radii.BottomLeft > 0.0f ? ImDrawFlags_RoundCornersBottomLeft : 0);
			}

			const ImVec2 min = ToImVec2(payload.Rect.Min, origin);
			const ImVec2 max = ToImVec2(payload.Rect.Max, origin);
			if (command.OutlineWidth > 0.0f)
			{
				drawList.AddRect(min, max, color, rounding, corners, command.OutlineWidth);
			}
			else
			{
				drawList.AddRectFilled(min, max, color, rounding, corners);
			}
		}

		void DrawCircleSectorWithImDrawList(ImDrawList& drawList, const PainterCommand& command, ImVec2 origin, ImU32 color)
		{
			const auto sector = GetPayload<PainterCircleSectorPayload>(command);
			const ImVec2 centre = ToImVec2(sector.Center, origin);
			const bool bFullRing = sector.EndAngle - sector.StartAngle >= 2.0f * 3.14159265f;

			if (bFullRing && command.OutlineWidth <= 0.0f)
			{
				// A ring is a circle drawn as thick as the ring.
				drawList.AddCircle(centre, 0.5f * (sector.InnerRadius + sector.OuterRadius), color, 0, sector.OuterRadius - sector.InnerRadius);
				return;
			}

			// Out along the outer arc, back along the inner one (or through the centre for a pie)
			drawList.PathArcTo(centre, sector.OuterRadius, sector.StartAngle, sector.EndAngle);
			if (sector.InnerRadius > 0.0f)
			{
				drawList.PathArcTo(centre, sector.InnerRadius, sector.EndAngle, sector.StartAngle);
			}
			else
			{
				drawList.PathLineTo(centre);
			}

			if (command.OutlineWidth > 0.0f)
			{
				drawList.PathStroke(color, ImDrawFlags_Closed, command.OutlineWidth);
			}
			else
			{
				drawList.PathFillConcave(color);
			}
		}
	}

	void PainterTestWindow::Draw(Nyx::IRenderer& renderer)
	{
		if (!bOpen)
		{
			Release();
			return;
		}
		Renderer = &renderer;

		ImGui::SetNextWindowSize(ImVec2(900.0f, 680.0f), ImGuiCond_FirstUseEver);
		// The wheel zooms the canvas instead of scrolling the window.
		if (!Nyx::UI::Begin("Painter Test", &bOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::End();
			return;
		}

		if (CanvasId == 0)
		{
			CanvasId = renderer.CreatePainterCanvas();
		}

		DrawToolbar();
		DrawStatistics();

		// The canvas fills the rest of the window, or its left half when comparing. Whole pixels,
		// so the painted image lands on the screen's pixels one to one.
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float canvasWidth = std::floor(bCompareWithImDrawList ? (available.x - spacing) * 0.5f : available.x);
		const ImVec2 canvasSize(std::max(canvasWidth, 1.0f), std::max(std::floor(available.y), 1.0f));
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();

		// The sheet at the current zoom and pan, in the canvas' pixels. The canvas is cleared to
		// the window's own opaque background: ImGui shows images with straight alpha, and the
		// painter writes premultiplied colour, which only match where the image is opaque.
		Painter.BeginFrame(static_cast<uint32_t>(canvasSize.x), static_cast<uint32_t>(canvasSize.y));
		Painter.PushTransform(Zoom, Pan);
		DrawPainterTestSheet(Painter, ImGui::GetFont(), ImGui::GetIO().Fonts->TexRef);
		Painter.PopTransform();
		Painter.EndFrame();

		const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		PainterDrawOptions options;
		options.ClearColor = glm::vec4(background.x, background.y, background.z, 1.0f);
		options.DebugView = bHeatmap ? EPainterDebugView::Heatmap : EPainterDebugView::None;
		renderer.SubmitPainterCanvas(CanvasId, Painter, options);

		// The renderer paints the canvas after the UI is built, before ImGui renders, so the image
		// shows this frame's sheet. It has no texture before its first draw.
		const ImTextureID canvasTexture = renderer.GetPainterCanvasTextureId(CanvasId);
		if (canvasTexture != ImTextureID_Invalid)
		{
			NYX_UI(ImGui::Image(canvasTexture, canvasSize));
		}
		else
		{
			ImGui::Dummy(canvasSize);
		}

		ImGui::SetCursorScreenPos(canvasMin);
		NYX_UI(ImGui::InvisibleButton("##PainterCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle));
		HandleCanvasInput(canvasMin);

		if (bCompareWithImDrawList)
		{
			ImGui::SameLine();
			const ImVec2 compareMin = ImGui::GetCursorScreenPos();
			const ImVec2 compareMax(compareMin.x + canvasSize.x, compareMin.y + canvasSize.y);
			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			drawList.PushClipRect(compareMin, compareMax, true);
			drawList.AddRectFilled(compareMin, compareMax, ImGui::GetColorU32(ImGuiCol_WindowBg));
			DrawPainterFrameWithImDrawList(Painter, drawList, compareMin);
			drawList.PopClipRect();
			ImGui::Dummy(canvasSize);
		}

		ImGui::End();
	}

	void PainterTestWindow::Release()
	{
		if (Renderer && CanvasId != 0)
		{
			Renderer->DestroyPainterCanvas(CanvasId);
		}
		CanvasId = 0;
	}

	void PainterTestWindow::DrawToolbar()
	{
		float zoomPercent = Zoom * 100.0f;
		ImGui::SetNextItemWidth(180.0f);
		if (NYX_UI(ImGui::SliderFloat("Zoom", &zoomPercent, MinZoom * 100.0f, MaxZoom * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic)))
		{
			Zoom = std::clamp(zoomPercent / 100.0f, MinZoom, MaxZoom);
		}

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("100%")))
		{
			Zoom = 1.0f;
			Pan = HomePan;
		}

		ImGui::SameLine();
		NYX_UI(ImGui::Checkbox("Tile heatmap", &bHeatmap));
		ImGui::SameLine();
		NYX_UI(ImGui::Checkbox("Compare with ImDrawList", &bCompareWithImDrawList));
		ImGui::SameLine();
		NYX_UI(ImGui::TextDisabled("Wheel zooms, drag pans"));
	}

	void PainterTestWindow::DrawStatistics() const
	{
		const PainterTiles* tiles = Renderer ? Renderer->GetPainterCanvasTiles(CanvasId) : nullptr;
		if (!tiles)
		{
			NYX_UI(ImGui::TextDisabled("Not painted yet"));
			return;
		}

		// The last painted frame; with the window open, that is the previous one.
		const PainterBinningStats& stats = tiles->Stats;
		NYX_UI(ImGui::Text("Commands %u (culled %u, dropped %u)   Groups %zu   Textures %zu",
			stats.Commands, stats.CulledCommands, stats.DroppedCommands, Painter.GetGroups().size(), Painter.GetTextures().size()));
		NYX_UI(ImGui::Text("Tiles in use %u of %zu   Commands per tile in use: %.1f on average, %u at most   Binning %.3f ms",
			stats.TilesInUse, tiles->Tiles.size(), stats.GetAverageCommandsPerTileInUse(), stats.MaxCommandsPerTile, stats.BinningMilliseconds));

		const ImVec4 problemColor(1.0f, 0.4f, 0.35f, 1.0f);
		for (const std::string& problem : Painter.GetProblems())
		{
			NYX_UI(ImGui::TextColored(problemColor, "%s", problem.c_str()));
		}
		for (const std::string& problem : tiles->Problems)
		{
			NYX_UI(ImGui::TextColored(problemColor, "%s", problem.c_str()));
		}
	}

	void PainterTestWindow::HandleCanvasInput(const ImVec2& canvasMin)
	{
		const ImGuiIO& io = ImGui::GetIO();

		if (ImGui::IsItemActive())
		{
			Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
		}

		if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
		{
			const float newZoom = std::clamp(Zoom * std::pow(1.15f, io.MouseWheel), MinZoom, MaxZoom);

			// The point of the sheet under the mouse stays under the mouse.
			const glm::vec2 mouse(io.MousePos.x - canvasMin.x, io.MousePos.y - canvasMin.y);
			Pan = mouse - (mouse - Pan) * (newZoom / Zoom);
			Zoom = newZoom;
		}
	}

	void DrawPainterFrameWithImDrawList(const Painter& painter, ImDrawList& drawList, ImVec2 origin)
	{
		const std::vector<ImTextureRef>& textures = painter.GetTextures();
		for (const PainterCommand& command : painter.GetCommands())
		{
			const ImU32 color = ToImGuiColor(command.Color);
			drawList.PushClipRect(ToImVec2(command.ClipRect.Min, origin), ToImVec2(command.ClipRect.Max, origin), true);

			switch (static_cast<EPainterCommandType>(command.Type))
			{
			case EPainterCommandType::Rect:
				DrawRectWithImDrawList(drawList, command, origin, color);
				break;
			case EPainterCommandType::Circle:
			{
				const auto circle = GetPayload<PainterCirclePayload>(command);
				if (command.OutlineWidth > 0.0f)
				{
					drawList.AddCircle(ToImVec2(circle.Center, origin), circle.Radius, color, 0, command.OutlineWidth);
				}
				else
				{
					drawList.AddCircleFilled(ToImVec2(circle.Center, origin), circle.Radius, color);
				}
				break;
			}
			case EPainterCommandType::CircleSector:
				DrawCircleSectorWithImDrawList(drawList, command, origin, color);
				break;
			case EPainterCommandType::Line:
			{
				// ImDrawList lines have no caps of their own.
				const auto line = GetPayload<PainterLinePayload>(command);
				drawList.AddLine(ToImVec2(line.From, origin), ToImVec2(line.To, origin), color, line.Width);
				break;
			}
			case EPainterCommandType::Triangle:
			{
				const auto triangle = GetPayload<PainterTrianglePayload>(command);
				const ImVec2 a = ToImVec2(triangle.A, origin);
				const ImVec2 b = ToImVec2(triangle.B, origin);
				const ImVec2 c = ToImVec2(triangle.C, origin);
				if (command.OutlineWidth > 0.0f)
				{
					drawList.AddTriangle(a, b, c, color, command.OutlineWidth);
				}
				else
				{
					drawList.AddTriangleFilled(a, b, c, color);
				}
				break;
			}
			case EPainterCommandType::Glyph:
			case EPainterCommandType::Image:
			{
				// ImGui draws its own text this way too: a textured quad per glyph.
				const auto textured = GetPayload<PainterTexturedRectPayload>(command);
				if (command.Texture < textures.size() && !IsReplacedThisFrame(textures[command.Texture]))
				{
					drawList.AddImage(textures[command.Texture], ToImVec2(textured.Rect.Min, origin), ToImVec2(textured.Rect.Max, origin),
						ImVec2(textured.UVs.Min.x, textured.UVs.Min.y), ImVec2(textured.UVs.Max.x, textured.UVs.Max.y), color);
				}
				break;
			}
			}

			drawList.PopClipRect();
		}
	}
}

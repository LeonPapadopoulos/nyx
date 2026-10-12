#include "PainterTestWindow.h"

#include "ImGuiSource.h"
#include "PainterDrawOptions.h"
#include "PainterTestSheet.h"
#include "Paths.h"
#include "Renderer.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <algorithm>
#include <cmath>
#include <ctime>
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

		if (ImGui::BeginTabBar("##PainterTestTabs"))
		{
			if (NYX_UI(ImGui::BeginTabItem("Canvas")))
			{
				DrawCanvasTab(renderer);
				ImGui::EndTabItem();
			}
			if (NYX_UI(ImGui::BeginTabItem("In a window")))
			{
				DrawInWindowTab(renderer);
				ImGui::EndTabItem();
			}
			if (NYX_UI(ImGui::BeginTabItem("Widgets")))
			{
				DrawWidgetsTab(renderer);
				ImGui::EndTabItem();
			}
			if (NYX_UI(ImGui::BeginTabItem("Blur")))
			{
				DrawBlurTab(renderer);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void PainterTestWindow::DrawCanvasTab(Nyx::IRenderer& renderer)
	{
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
		DrawPainterTestSheet(Painter, ImGui::GetFont(), ImGui::GetIO().Fonts->TexRef, GetGradientTexture(renderer));
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
	}

	void PainterTestWindow::DrawInWindowTab(Nyx::IRenderer& renderer)
	{
		DrawWindowPainterStatistics(renderer.GetWindowPainterStats());

		// The sheet painted straight into a child window that scrolls, so its segments are
		// clipped by the child like ImGui's own drawing.
		if (Nyx::UI::BeginChild("##PainterInWindow", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
			ImFont* font = ImGui::GetFont();
			const ImTextureRef fontAtlas = ImGui::GetIO().Fonts->TexRef;
			const ImTextureRef gradient = GetGradientTexture(renderer);

			for (int row = 0; row < PainterTestSheetRowCount; ++row)
			{
				// The sheet's row, moved to where ImGui's cursor is
				const ImVec2 rowMin = ImGui::GetCursorScreenPos();
				painter.PushTransform(1.0f, { rowMin.x, rowMin.y - GetPainterTestSheetRowTop(row) });
				DrawPainterTestSheetRow(painter, row, font, fontAtlas, gradient);
				painter.PopTransform();

				// An ImGui button over the row's right end, then a painted badge on the button: on
				// the screen, each covers what came before it in the code.
				ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 590.0f, rowMin.y + 28.0f));
				ImGui::PushID(row);
				NYX_UI(ImGui::Button("ImGui over the row"));
				ImGui::PopID();
				const glm::vec2 badge(ImGui::GetItemRectMax().x - 3.0f, ImGui::GetItemRectMin().y + 3.0f);
				painter.AddCircle(badge, 6.0f, PainterStyle::Filled(glm::vec4(1.0f, 0.55f, 0.15f, 1.0f)));

				// The last row (text and images) is taller than the others.
				const bool bLastRow = row == PainterTestSheetRowCount - 1;
				const float rowHeight = bLastRow ? PainterTestSheetSize.y - GetPainterTestSheetRowTop(row) : PainterTestSheetRowHeight;
				ImGui::SetCursorScreenPos(rowMin);
				ImGui::Dummy(ImVec2(PainterTestSheetSize.x, rowHeight));
			}
		}
		ImGui::EndChild();
	}

	void PainterTestWindow::DrawWidgetsTab(Nyx::IRenderer& renderer)
	{
		// Saves the gallery as it shows, to compare a widget before and after a change
		if (NYX_UI(Nyx::UI::Button("Save PNG")))
		{
			SaveGalleryPng(renderer);
		}
		ImGui::SameLine();
		const std::string& message = GalleryMessage.empty() ? renderer.GetScreenshotMessage() : GalleryMessage;
		NYX_UI(ImGui::TextDisabled("%s", message.empty() ? "Each painted widget in every state, forced; then live, and ImGui's own beside it" : message.c_str()));

		if (Nyx::UI::BeginChild("##WidgetGallery", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			// Where the gallery shows, for Save PNG next frame
			GalleryMin = ImGui::GetWindowPos();
			GalleryMax = ImVec2(GalleryMin.x + ImGui::GetWindowWidth(), GalleryMin.y + ImGui::GetWindowHeight());
			bGalleryInMainWindow = ImGui::GetWindowViewport() == ImGui::GetMainViewport();

			// The font scale of a 100% and a 150% screen
			for (const float scale : { 1.0f, 1.5f })
			{
				ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
				ImGui::PushID(scale == 1.0f ? "100" : "150");
				NYX_UI(Nyx::UI::SectionHeader(scale == 1.0f ? "At 100%" : "At 150%"));
				DrawWidgetGalleryTable();
				ImGui::PopID();
				ImGui::PopFont();
				ImGui::Spacing();
			}
		}
		ImGui::EndChild();
	}

	void PainterTestWindow::DrawBlurTab(Nyx::IRenderer& renderer)
	{
		const std::vector<uint64_t> views = renderer.GetSceneViewIds();
		if (views.empty())
		{
			NYX_UI(ImGui::TextDisabled("No scene view to blur"));
			return;
		}
		// Each view by its number and size; until one is chosen, the largest, which is the one on show
		const auto viewName = [&](size_t index)
		{
			const Nyx::Extent2D size = renderer.GetSceneViewExtent(views[index]);
			return "Scene view " + std::to_string(index + 1) + ", " + std::to_string(size.Width) + " x " + std::to_string(size.Height);
		};
		if (!bBlurViewChosen)
		{
			const auto area = [&](size_t index)
			{
				const Nyx::Extent2D size = renderer.GetSceneViewExtent(views[index]);
				return static_cast<uint64_t>(size.Width) * size.Height;
			};
			BlurViewIndex = 0;
			for (size_t index = 1; index < views.size(); ++index)
			{
				BlurViewIndex = area(index) > area(BlurViewIndex) ? index : BlurViewIndex;
			}
		}
		BlurViewIndex = std::min(BlurViewIndex, views.size() - 1);
		const uint64_t view = views[BlurViewIndex];

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
		if (NYX_UI(ImGui::BeginCombo("##BlurView", viewName(BlurViewIndex).c_str())))
		{
			for (size_t index = 0; index < views.size(); ++index)
			{
				if (NYX_UI(ImGui::Selectable(viewName(index).c_str(), index == BlurViewIndex)))
				{
					BlurViewIndex = index;
					bBlurViewChosen = true;
				}
			}
			ImGui::EndCombo();
		}

		// The time is the GPU's, for every scene view's blur together, a frame or two ago.
		ImGui::SameLine();
		const Nyx::Extent2D blurSize = renderer.GetSceneViewBlurExtent(view);
		const float milliseconds = renderer.GetBlurMilliseconds();
		if (milliseconds >= 0.0f)
		{
			NYX_UI(ImGui::Text("Blurred to %u x %u   All scene views' blurs: %.3f ms on the GPU", blurSize.Width, blurSize.Height, milliseconds));
		}
		else
		{
			NYX_UI(ImGui::Text("Blurred to %u x %u   (this GPU can't time it)", blurSize.Width, blurSize.Height));
		}

		const ImTextureID picture = renderer.GetSceneViewTextureId(view);
		const ImTextureID blurred = renderer.GetSceneViewBlurTextureId(view);
		if (picture == ImTextureID{} || picture == ImTextureID_Invalid || blurred == ImTextureID_Invalid || blurSize.Width == 0)
		{
			NYX_UI(ImGui::TextDisabled("The scene view hasn't been drawn yet"));
			return;
		}

		// The picture, its blur as ImGui shows it, and the blur painted, as glows and glass will
		// sample it: three side by side, as wide as fits
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float width = std::floor((ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
		const glm::vec2 size(width, std::floor(width * static_cast<float>(blurSize.Height) / static_cast<float>(blurSize.Width)));
		const auto column = [&](const char* caption, const auto& draw)
		{
			ImGui::BeginGroup();
			NYX_UI(ImGui::TextDisabled("%s", caption));
			draw();
			ImGui::EndGroup();
		};
		column("Scene view", [&]() { NYX_UI(ImGui::Image(picture, ImVec2(size.x, size.y))); });
		ImGui::SameLine();
		column("Blurred, by ImGui", [&]() { NYX_UI(ImGui::Image(blurred, ImVec2(size.x, size.y))); });
		ImGui::SameLine();
		column("Blurred, painted", [&]()
			{
				Nyx::UI::GetWindowPainter().AddImage(ImTextureRef(blurred), ReserveCell(size), PainterStyle::Filled(glm::vec4(1.0f)));
			});
	}

	void PainterTestWindow::SaveGalleryPng(Nyx::IRenderer& renderer)
	{
		// The renderer reads back the main window only; ImGui makes its other OS windows' images
		// without the right to copy from them.
		if (!bGalleryInMainWindow)
		{
			GalleryMessage = "Only the main window can be saved: dock or move the Painter Test window into it";
			return;
		}
		GalleryMessage.clear();

		// Saved/Gallery/Gallery 2026-10-12 14-03-27.png, by local time
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		char fileName[64];
		std::strftime(fileName, sizeof(fileName), "Gallery %Y-%m-%d %H-%M-%S.png", &local);
		renderer.SaveScreenshot(Nyx::Paths::GetProjectRoot() / "Saved" / "Gallery" / fileName, GalleryMin, GalleryMax);
	}

	void PainterTestWindow::DrawWidgetGalleryTable()
	{
		using Nyx::UI::WidgetLook;

		// The states a widget can show; the gallery paints them without the mouse.
		const char* const stateNames[] = { "Normal", "Hovered", "Pressed", "Focused", "Disabled" };
		const WidgetLook states[] = {
			WidgetLook{},
			WidgetLook{ 1.0f, 0.0f, 0.0f, false },
			WidgetLook{ 1.0f, 1.0f, 0.0f, false },
			WidgetLook{ 0.0f, 0.0f, 1.0f, false },
			WidgetLook{ 0.0f, 0.0f, 0.0f, true },
		};
		constexpr int StateCount = static_cast<int>(std::size(stateNames));

		// Room around each cell, so that shadows and focus glows aren't cut off at its edges
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12.0f, 10.0f));
		if (!ImGui::BeginTable("##Gallery", StateCount + 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::PopStyleVar();
			return;
		}
		ImGui::TableSetupColumn("Widget");
		for (const char* stateName : stateNames)
		{
			ImGui::TableSetupColumn(stateName);
		}
		ImGui::TableSetupColumn("Live");
		ImGui::TableSetupColumn("ImGui");
		ImGui::TableHeadersRow();

		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();

		// Sliders get their width each: a table sets the item width per column.
		const float sliderWidth = ImGui::GetFontSize() * 10.0f;

		// One row: the widget's name, `paint` once per state, the live painted widget, and ImGui's
		const auto row = [&](const char* name, const auto& paint, const auto& live, const auto& imgui)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			NYX_UI(ImGui::AlignTextToFramePadding());
			NYX_UI(ImGui::TextUnformatted(name));
			for (const WidgetLook& look : states)
			{
				ImGui::TableNextColumn();
				paint(look);
			}
			ImGui::TableNextColumn();
			live();
			ImGui::TableNextColumn();
			imgui();
		};

		row("Button",
			[&](const WidgetLook& look) { Nyx::UI::PaintButton(painter, ReserveCell(Nyx::UI::CalcButtonSize("Apply")), "Apply", look); },
			[&]() { NYX_UI(Nyx::UI::Button("Apply##Live")); },
			[&]() { NYX_UI(ImGui::Button("Apply##ImGui")); });

		row("Toggle, off",
			[&](const WidgetLook& look) { Nyx::UI::PaintToggle(painter, ReserveCell(Nyx::UI::CalcToggleSize("Snap")), "Snap", 0.0f, look); },
			[&]() { NYX_UI(Nyx::UI::Toggle("Snap##Live", &bGalleryToggle)); },
			[&]() { NYX_UI(ImGui::Checkbox("Snap##ImGui", &bGalleryToggle)); });

		row("Toggle, on",
			[&](const WidgetLook& look) { Nyx::UI::PaintToggle(painter, ReserveCell(Nyx::UI::CalcToggleSize("Snap")), "Snap", 1.0f, look); },
			[&]() {},
			[&]() {});

		const float valueWidth = Nyx::UI::CalcSliderValueWidth(0.0f, 1.0f, "%.3f");
		row("Slider",
			[&](const WidgetLook& look)
			{
				// The frame, and room for the label right of it
				ImGui::SetNextItemWidth(sliderWidth);
				const glm::vec2 frameSize = Nyx::UI::CalcSliderFrameSize();
				const float labelWidth = ImGui::CalcTextSize("Gain").x + ImGui::GetStyle().ItemInnerSpacing.x;
				const PainterRect cell = ReserveCell(frameSize + glm::vec2(labelWidth, 0.0f));
				Nyx::UI::PaintSlider(painter, PainterRect{ cell.Min, cell.Min + frameSize }, "Gain", 0.6f, "0.600", valueWidth, look);
			},
			[&]()
			{
				ImGui::SetNextItemWidth(sliderWidth);
				NYX_UI(Nyx::UI::SliderFloat("Gain##Live", &GallerySlider, 0.0f, 1.0f));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(sliderWidth);
				NYX_UI(ImGui::SliderFloat("Gain##ImGui", &GallerySlider, 0.0f, 1.0f));
			});

		// A section header has no states; it shows once, in the Normal column.
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		NYX_UI(ImGui::AlignTextToFramePadding());
		NYX_UI(ImGui::TextUnformatted("Section header"));
		ImGui::TableNextColumn();
		Nyx::UI::PaintSectionHeader(painter, ReserveCell(glm::vec2(ImGui::GetFontSize() * 9.0f, ImGui::GetFrameHeight())), "Lighting");
		ImGui::TableSetColumnIndex(StateCount + 1);
		NYX_UI(Nyx::UI::SectionHeader("Lighting##Live"));
		ImGui::TableNextColumn();
		NYX_UI(ImGui::SeparatorText("Lighting##ImGui"));

		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	PainterRect PainterTestWindow::ReserveCell(glm::vec2 size)
	{
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(size.x, size.y));
		return PainterRect{ { min.x, min.y }, { min.x + size.x, min.y + size.y } };
	}

	ImTextureRef PainterTestWindow::GetGradientTexture(Nyx::IRenderer& renderer)
	{
		if (GradientImageId == 0)
		{
			GradientImageId = renderer.CreateUIImage(MakePainterTestGradient());
		}
		return ImTextureRef(renderer.GetUIImageTextureId(GradientImageId));
	}

	void PainterTestWindow::DrawWindowPainterStatistics(const PainterWindowStats& stats) const
	{
		// The last drawn frame: the previous one
		NYX_UI(ImGui::Text("Windows %u   Segments %u   Commands %u   Tiles in use %u (%u entries)   Binning %.3f ms",
			stats.Windows, stats.Segments, stats.Commands, stats.TilesInUse, stats.TileEntries, stats.BinningMilliseconds));

		const ImVec4 problemColor(1.0f, 0.4f, 0.35f, 1.0f);
		for (const std::string& problem : stats.Problems)
		{
			NYX_UI(ImGui::TextColored(problemColor, "%s", problem.c_str()));
		}
	}

	void PainterTestWindow::Release()
	{
		if (Renderer && CanvasId != 0)
		{
			Renderer->DestroyPainterCanvas(CanvasId);
		}
		if (Renderer && GradientImageId != 0)
		{
			Renderer->DestroyUIImage(GradientImageId);
		}
		CanvasId = 0;
		GradientImageId = 0;
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
			case EPainterCommandType::ImageGlow:
				// ImDrawList has no glows; left out like the groups' shadows and glows.
				break;
			}

			drawList.PopClipRect();
		}
	}
}

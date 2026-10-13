#include "PainterTestWindow.h"

#include "ImGuiSource.h"
#include "PainterDrawOptions.h"
#include "PainterTestSheet.h"
#include "Paths.h"
#include "Renderer.h"
#include "UIDiagnostics.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <span>
#include <string>
#include <tuple>
#include <utility>

namespace Nyx::Editor
{
	using namespace Nyx::Engine;

	namespace
	{
		constexpr float MinZoom = 0.25f;
		constexpr float MaxZoom = 8.0f;
		const glm::vec2 HomePan{ 16.0f, 16.0f };

		// The Statistics tab's graph lines: a debugging aid, not part of the look, so not the theme's
		const glm::vec4 GraphBuildUIColor{ 0.45f, 0.65f, 0.95f, 1.0f };
		const glm::vec4 GraphPainterCpuColor{ 1.0f, 0.55f, 0.15f, 1.0f };
		const glm::vec4 GraphPainterGpuColor{ 0.50f, 0.80f, 0.36f, 1.0f };
		const glm::vec4 GraphMainWindowGpuColor{ 0.80f, 0.50f, 0.90f, 1.0f };

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

	void PainterTestWindow::SetFrameRateLimits(bool bVSyncOn, uint32_t frameRateCap)
	{
		bVSync = bVSyncOn;
		FrameRateCap = frameRateCap;
	}

	void PainterTestWindow::Draw(Nyx::IRenderer& renderer)
	{
		if (!bOpen)
		{
			Release();
			return;
		}
		Renderer = &renderer;
		RecordFrameSample(renderer);

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
			if (NYX_UI(ImGui::BeginTabItem("Statistics")))
			{
				DrawStatisticsTab(renderer);
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
		if (NYX_UI(Nyx::UI::BeginCombo("##BlurView", viewName(BlurViewIndex).c_str())))
		{
			for (size_t index = 0; index < views.size(); ++index)
			{
				if (NYX_UI(Nyx::UI::Selectable(viewName(index).c_str(), index == BlurViewIndex)))
				{
					BlurViewIndex = index;
					bBlurViewChosen = true;
				}
			}
			Nyx::UI::EndCombo();
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

	void PainterTestWindow::RecordFrameSample(const Nyx::IRenderer& renderer)
	{
		const PainterWindowStats& stats = renderer.GetWindowPainterStats();
		const PainterWindowStats::Times& times = stats.FrameTimes;
		FrameSample sample;
		sample.Frame = times.Frame;
		sample.PainterTotal = times.GetPainterCpu() + times.GpuPainter;
		sample.BuildUI = times.BuildUI;
		sample.Chrome = times.Chrome;
		sample.Gather = times.Gather;
		sample.ImGuiRender = times.ImGuiRender;
		sample.Segments = times.Segments;
		sample.Binning = stats.BinningMilliseconds;
		sample.PainterCpu = times.GetPainterCpu();
		sample.GpuBinning = times.GpuBinning;
		sample.GpuPainter = times.GpuPainter;
		sample.GpuMainWindowUI = times.GpuMainWindowUI;
		sample.GpuGlassBackdrop = times.GpuGlassBackdrop;
		// Negative while the blur isn't timed
		sample.GpuBlur = std::max(renderer.GetBlurMilliseconds(), 0.0f);
		sample.Outside = times.Outside;
		sample.WaitForGpu = times.WaitForGpu;
		sample.Acquire = times.Acquire;
		sample.Scene = times.Scene;
		sample.Canvases = times.Canvases;
		sample.RecordUI = times.RecordUI;
		sample.Present = times.Present;
		// The frame is timed from start to start, the lines within it: what they leave, at least 0
		const float lines = times.Outside + times.WaitForGpu + times.Acquire + times.Scene + times.BuildUI + times.Chrome + times.Canvases +
			times.Gather + times.ImGuiRender + times.Segments + times.RecordUI + times.Present;
		sample.Rest = std::max(times.Frame - lines, 0.0f);
		sample.GpuEditorPasses = times.GpuEditorPasses;
		sample.GpuSceneViews = times.GpuSceneViews;
		sample.GpuFrame = times.GpuFrame;
		sample.Draws = static_cast<float>(stats.Draws);
		sample.Commands = static_cast<float>(stats.Commands);
		sample.EntriesBefore = static_cast<float>(stats.Reduction.EntriesBefore);
		sample.Entries = static_cast<float>(stats.Reduction.Kept);
		sample.KeptCardsOpaque = static_cast<float>(stats.Reduction.KeptIfAlsoOpaque);

		if (FrameSamples.size() < StatisticsFrames)
		{
			FrameSamples.push_back(sample);
		}
		else
		{
			FrameSamples[NextFrameSample] = sample;
		}
		NextFrameSample = (NextFrameSample + 1) % StatisticsFrames;
	}

	std::vector<PainterTestWindow::FrameSample> PainterTestWindow::GetFrameSamples() const
	{
		if (FrameSamples.size() < StatisticsFrames)
		{
			return FrameSamples;
		}
		std::vector<FrameSample> ordered(FrameSamples.begin() + static_cast<std::ptrdiff_t>(NextFrameSample), FrameSamples.end());
		ordered.insert(ordered.end(), FrameSamples.begin(), FrameSamples.begin() + static_cast<std::ptrdiff_t>(NextFrameSample));
		return ordered;
	}

	void PainterTestWindow::DrawStatisticsTab(Nyx::IRenderer& renderer)
	{
		const PainterWindowStats& stats = renderer.GetWindowPainterStats();
		const std::vector<FrameSample> samples = GetFrameSamples();
		if (samples.empty())
		{
			return;
		}

		if (Nyx::UI::BeginChild("##Statistics", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None))
		{
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::TextDisabled("The last frame, and the average and peak of the last %zu frames while this window is open. GPU times come from "
									   "timestamps. A Debug build's CPU times are several times a Release build's.",
				samples.size()));
			ImGui::PopTextWrapPos();

			// A table of the frame: each line's last value, average and peak over the samples
			struct Line
			{
				const char* Name = "";
				float FrameSample::* Field = nullptr;
				const char* Format = "%.3f";
			};
			const auto drawLines = [&](const char* id, const char* unit, std::span<const Line> lines)
			{
				if (!Nyx::UI::BeginTable(id, 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
				{
					return;
				}
				ImGui::TableSetupColumn(unit, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 18.0f);
				for (const char* column : { "Last", "Average", "Peak" })
				{
					ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
				}
				Nyx::UI::TableHeadersRow();
				for (const Line& line : lines)
				{
					float sum = 0.0f;
					float peak = 0.0f;
					for (const FrameSample& sample : samples)
					{
						sum += sample.*line.Field;
						peak = std::max(peak, sample.*line.Field);
					}
					Nyx::UI::TableNextRow();
					ImGui::TableNextColumn();
					NYX_UI(ImGui::TextUnformatted(line.Name));
					for (const float value : { samples.back().*line.Field, sum / static_cast<float>(samples.size()), peak })
					{
						ImGui::TableNextColumn();
						NYX_UI(ImGui::Text(line.Format, value));
					}
				}
				Nyx::UI::EndTable();
			};

			NYX_UI(Nyx::UI::SectionHeader("Frame"));
			const Line frame[] = {
				{ "The editor's frame", &FrameSample::Frame },
				{ "The painter, CPU and GPU", &FrameSample::PainterTotal },
			};
			drawLines("##Frame", "Milliseconds", frame);
			// Frame rates from the average times, and the slowest frame's; the painter alone as if
			// it were all the editor did
			float frameSum = 0.0f;
			float slowestFrame = 0.0f;
			float painterSum = 0.0f;
			for (const FrameSample& sample : samples)
			{
				frameSum += sample.Frame;
				slowestFrame = std::max(slowestFrame, sample.Frame);
				painterSum += sample.PainterTotal;
			}
			const float averageFrame = frameSum / static_cast<float>(samples.size());
			const float averagePainter = painterSum / static_cast<float>(samples.size());
			const auto perSecond = [](float milliseconds)
			{
				return milliseconds > 0.0f ? 1000.0f / milliseconds : 0.0f;
			};
			NYX_UI(ImGui::Text("The editor: %.0f fps on average, %.0f at its slowest frame", perSecond(averageFrame), perSecond(slowestFrame)));
			NYX_UI(ImGui::Text("The painter alone: %.0f fps, %.0f%% of the frame", perSecond(averagePainter),
				averageFrame > 0.0f ? averagePainter / averageFrame * 100.0f : 0.0f));
			DrawFrameRateLimitInfo();
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::TextDisabled("The editor runs one frame at a time: each frame waits for the last one's GPU work, so the painter's CPU and GPU "
									   "times add up."));
			ImGui::PopTextWrapPos();

			NYX_UI(Nyx::UI::SectionHeader("CPU"));
			// The frame's steps in order, then the painter's share of them
			const Line cpu[] = {
				{ "Outside: events, updates, the cap", &FrameSample::Outside },
				{ "Waiting for the last frame's GPU", &FrameSample::WaitForGpu },
				{ "Acquiring the next image", &FrameSample::Acquire },
				{ "The world and the scene views", &FrameSample::Scene },
				{ "Building the UI", &FrameSample::BuildUI },
				{ "Painting the chrome", &FrameSample::Chrome },
				{ "Canvases, marks, heatmap", &FrameSample::Canvases },
				{ "Gathering the painters", &FrameSample::Gather },
				{ "ImGui::Render()", &FrameSample::ImGuiRender },
				{ "Segments: runs, measuring, upload", &FrameSample::Segments },
				{ "    of which measuring", &FrameSample::Binning },
				{ "Recording and submitting the UI", &FrameSample::RecordUI },
				{ "Presenting", &FrameSample::Present },
				{ "The rest of the frame", &FrameSample::Rest },
				{ "The painter after the UI", &FrameSample::PainterCpu },
			};
			drawLines("##Cpu", "Milliseconds", cpu);
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::TextDisabled("The frame stands still while it waits: for the last frame's GPU work, which with vsync also waits "
									   "for the monitor to free an image, and for the frame rate cap (outside)."));
			ImGui::PopTextWrapPos();

			NYX_UI(Nyx::UI::SectionHeader("GPU"));
			if (!stats.FrameTimes.bGpu)
			{
				NYX_UI(ImGui::TextDisabled("Not timed: the graphics queue can't write timestamps."));
			}
			else
			{
				const Line gpu[] = {
					{ "The frame's command buffer", &FrameSample::GpuFrame },
					{ "Picking, selection masks", &FrameSample::GpuEditorPasses },
					{ "The scene views", &FrameSample::GpuSceneViews },
					{ "The scene views' blurs", &FrameSample::GpuBlur },
					{ "Binning", &FrameSample::GpuBinning },
					{ "The glass backdrop", &FrameSample::GpuGlassBackdrop },
					{ "The main window's UI pass", &FrameSample::GpuMainWindowUI },
					{ "The painter's draws", &FrameSample::GpuPainter },
				};
				drawLines("##Gpu", "Milliseconds", gpu);
				ImGui::PushTextWrapPos(0.0f);
				NYX_UI(ImGui::TextDisabled("The command buffer holds all but OS windows' drawing. The painter's draws are within the UI pass "
										   "and the glass backdrop, and in OS windows. Picking only runs for a click, the selection masks "
										   "only when what they show changed."));
				ImGui::PopTextWrapPos();
			}

			NYX_UI(Nyx::UI::SectionHeader("Painting"));
			const Line counts[] = {
				{ "Draws", &FrameSample::Draws, "%.0f" },
				{ "Commands", &FrameSample::Commands, "%.0f" },
			};
			drawLines("##Counts", "Count", counts);
			NYX_UI(ImGui::Text("Windows painting %u   Segments %u   Tiles in use %u (%u entries)   Uploaded %.1f KB", stats.Windows, stats.Segments,
				stats.TilesInUse, stats.TileEntries, static_cast<float>(stats.UploadBytes) / 1024.0f));

			NYX_UI(Nyx::UI::SectionHeader("Per OS window"));
			DrawOsWindowStatistics(stats);
			// Where on the screen the painter's work lies (also Debug > Painter Heatmap)
			constexpr const char* HeatmapViews[] = { "Off", "Commands per tile", "Draws per tile" };
			int heatmap = static_cast<int>(Nyx::UI::GetPainterHeatmap());
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (NYX_UI(Nyx::UI::Combo("Heatmap over the editor", &heatmap, HeatmapViews)))
			{
				Nyx::UI::SetPainterHeatmap(static_cast<Nyx::UI::EPainterHeatmap>(heatmap));
			}

			NYX_UI(Nyx::UI::SectionHeader("The per-tile reduction"));
			DrawReduction(samples);

			NYX_UI(Nyx::UI::SectionHeader("The last frames"));
			DrawFrameGraph(samples);

			const ImVec4 problemColor(1.0f, 0.4f, 0.35f, 1.0f);
			for (const std::string& problem : stats.Problems)
			{
				NYX_UI(ImGui::TextColored(problemColor, "%s", problem.c_str()));
			}
		}
		ImGui::EndChild();
	}

	void PainterTestWindow::DrawOsWindowStatistics(const PainterWindowStats& stats) const
	{
		constexpr const char* Columns[] = { "OS window", "Segments", "Draws", "Commands", "Tiles", "Binning ms", "GPU ms", "ImGui draws", "ImGui vertices" };
		if (!Nyx::UI::BeginTable("##OsWindows", static_cast<int>(std::size(Columns)), ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
		{
			return;
		}
		for (const char* column : Columns)
		{
			ImGui::TableSetupColumn(column);
		}
		Nyx::UI::TableHeadersRow();
		for (const PainterWindowStats::OsWindow& osWindow : stats.OsWindows)
		{
			Nyx::UI::TableNextRow();
			ImGui::TableNextColumn();
			NYX_UI(ImGui::TextUnformatted(osWindow.Name.c_str()));
			for (const uint32_t count : { osWindow.Segments, osWindow.Draws, osWindow.Commands, osWindow.TilesInUse })
			{
				ImGui::TableNextColumn();
				NYX_UI(ImGui::Text("%u", count));
			}
			ImGui::TableNextColumn();
			NYX_UI(ImGui::Text("%.3f", osWindow.BinningMilliseconds));
			ImGui::TableNextColumn();
			NYX_UI(ImGui::Text("%.3f", osWindow.GpuMilliseconds));
			ImGui::TableNextColumn();
			NYX_UI(ImGui::Text("%u", osWindow.ImGuiDrawCommands));
			ImGui::TableNextColumn();
			NYX_UI(ImGui::Text("%u", osWindow.ImGuiVertices));
		}
		Nyx::UI::EndTable();
	}

	void PainterTestWindow::DrawReduction(std::span<const FrameSample> samples) const
	{
		// The averages over the frames kept
		PainterCosts now;
		float entriesBefore = 0.0f;
		for (const FrameSample& sample : samples)
		{
			now.Frame += sample.Frame;
			now.PainterCpu += sample.PainterCpu;
			now.PainterGpu += sample.GpuPainter;
			now.Kept += sample.Entries;
			now.KeptCardsOpaque += sample.KeptCardsOpaque;
			entriesBefore += sample.EntriesBefore;
		}
		const float count = static_cast<float>(samples.size());
		for (float* value : { &now.Frame, &now.PainterCpu, &now.PainterGpu, &now.Kept, &now.KeptCardsOpaque, &entriesBefore })
		{
			*value /= count;
		}

		const auto share = [&](float kept)
		{
			return entriesBefore > 0.0f ? kept / entriesBefore * 100.0f : 100.0f;
		};
		NYX_UI(ImGui::Text("Tile entries %.0f before the reduction: it keeps %.0f (%.0f%%), and would keep %.0f (%.0f%%) with the docked cards opaque",
			entriesBefore, now.Kept, share(now.Kept), now.KeptCardsOpaque, share(now.KeptCardsOpaque)));

		const std::vector<PainterScenario> scenarios = PredictOpaqueDockedCards(now);
		if (Nyx::UI::BeginTable("##Predictions", static_cast<int>(scenarios.size()) + 1, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12.0f);
			for (const PainterScenario& scenario : scenarios)
			{
				ImGui::TableSetupColumn(scenario.Name);
			}
			Nyx::UI::TableHeadersRow();
			const auto row = [&](const char* name, const auto& value, const char* format)
			{
				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				NYX_UI(ImGui::TextUnformatted(name));
				for (const PainterScenario& scenario : scenarios)
				{
					ImGui::TableNextColumn();
					NYX_UI(ImGui::Text(format, value(scenario)));
				}
			};
			const auto perSecond = [](float milliseconds)
			{
				return milliseconds > 0.0f ? 1000.0f / milliseconds : 0.0f;
			};
			row("Painter CPU, ms", [](const PainterScenario& scenario) { return scenario.PainterCpu; }, "%.3f");
			row("Painter GPU, ms", [](const PainterScenario& scenario) { return scenario.PainterGpu; }, "%.3f");
			row("The painter alone, fps", [&](const PainterScenario& scenario) { return perSecond(scenario.PainterCpu + scenario.PainterGpu); }, "%.0f");
			row("The editor, fps", [&](const PainterScenario& scenario) { return perSecond(scenario.Frame); }, "%.0f");
			Nyx::UI::EndTable();
		}
		DrawFrameRateLimitInfo();

		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextDisabled("Opaque docked cards are an estimate: the GPU counts what the reduction would keep, and the painter's draws are taken "
								   "to go down with the entries their pixels walk. Ambilight would then no longer show through the cards."));
		ImGui::PopTextWrapPos();
	}

	void PainterTestWindow::DrawFrameRateLimitInfo() const
	{
		if (!bVSync && FrameRateCap == 0)
		{
			return;
		}

		// The frame includes the wait for the monitor or the cap; the painter's and the other
		// times are the work alone.
		std::string limit;
		if (bVSync && FrameRateCap > 0)
		{
			limit = "Vsync is on and the frame rate is capped at " + std::to_string(FrameRateCap) +
				" fps: the editor's frame rate is held to the lower of the monitor's refresh rate and the cap.";
		}
		else if (bVSync)
		{
			limit = "Vsync is on: the editor's frame rate is held to the monitor's refresh rate.";
		}
		else
		{
			limit = "The frame rate is capped at " + std::to_string(FrameRateCap) + " fps: the editor's frame rate is held to it.";
		}
		limit += " Its frame includes the wait; the painter's frame rate and the CPU and GPU times are the work alone, though "
				 "a GPU that waits often clocks down and takes longer for it (Window > Preferences, Frame rate).";

		const glm::vec4& info = Nyx::UI::GetTheme().TextInfo;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(info.r, info.g, info.b, info.a));
		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextUnformatted(("Info: " + limit).c_str()));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	void PainterTestWindow::DrawFrameGraph(std::span<const FrameSample> samples) const
	{
		struct Series
		{
			const char* Name = "";
			float FrameSample::* Field = nullptr;
			glm::vec4 Color{ 1.0f };
		};
		const Series series[] = {
			{ "Building the UI (CPU)", &FrameSample::BuildUI, GraphBuildUIColor },
			{ "The painter after the UI (CPU)", &FrameSample::PainterCpu, GraphPainterCpuColor },
			{ "The painter's draws (GPU)", &FrameSample::GpuPainter, GraphPainterGpuColor },
			{ "The main window's UI pass (GPU)", &FrameSample::GpuMainWindowUI, GraphMainWindowGpuColor },
		};
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();

		// The legend: a swatch and a name per line
		const float swatch = std::floor(ImGui::GetFontSize() * 0.7f);
		for (const Series& line : series)
		{
			if (&line != &series[0])
			{
				ImGui::SameLine(0.0f, ImGui::GetFontSize());
			}
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float top = at.y + std::floor((ImGui::GetTextLineHeight() - swatch) * 0.5f);
			painter.AddRoundedRect(PainterRect{ { at.x, top }, { at.x + swatch, top + swatch } }, swatch * 0.25f, PainterStyle::Filled(line.Color));
			ImGui::SetCursorScreenPos(ImVec2(at.x + swatch + ImGui::GetStyle().ItemInnerSpacing.x, at.y));
			NYX_UI(ImGui::TextUnformatted(line.Name));
		}

		// The scale: the highest time shown, rounded up to 1, 2 or 5 times a power of 10, at least half a millisecond
		float highest = 0.0f;
		for (const FrameSample& sample : samples)
		{
			for (const Series& line : series)
			{
				highest = std::max(highest, sample.*line.Field);
			}
		}
		float scale = 0.5f;
		for (float decade = 1.0f; scale < highest; decade *= 10.0f)
		{
			for (const float factor : { 1.0f, 2.0f, 5.0f })
			{
				if (scale < highest)
				{
					scale = factor * decade;
				}
			}
		}

		const glm::vec2 size(std::max(ImGui::GetContentRegionAvail().x, 1.0f), ImGui::GetFontSize() * 9.0f);
		const PainterRect area = ReserveCell(size);
		painter.AddRoundedRect(area, theme.FrameRounding, PainterStyle::Filled(theme.Field));
		const float middle = std::floor((area.Min.y + area.Max.y) * 0.5f) + 0.5f;
		painter.AddLine({ area.Min.x, middle }, { area.Max.x, middle }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.Separator));
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		char label[32];
		std::snprintf(label, sizeof(label), "%g ms", scale);
		drawList->AddText(ImVec2(area.Min.x + 6.0f, area.Min.y + 4.0f), labelColor, label);
		std::snprintf(label, sizeof(label), "%g ms", scale * 0.5f);
		drawList->AddText(ImVec2(area.Min.x + 6.0f, middle + 2.0f), labelColor, label);

		// The newest frame at the right, one step per frame
		const float step = area.GetWidth() / static_cast<float>(StatisticsFrames - 1);
		const auto pointOf = [&](size_t index, float value)
		{
			const float x = area.Max.x - static_cast<float>(samples.size() - 1 - index) * step;
			const float y = area.Max.y - std::clamp(value / scale, 0.0f, 1.0f) * (area.GetHeight() - 2.0f) - 1.0f;
			return glm::vec2(x, y);
		};
		for (const Series& line : series)
		{
			for (size_t index = 1; index < samples.size(); ++index)
			{
				painter.AddLine(pointOf(index - 1, samples[index - 1].*line.Field), pointOf(index, samples[index].*line.Field), 1.5f, EPainterLineCap::Round,
					PainterStyle::Filled(line.Color));
			}
		}
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
		if (!Nyx::UI::BeginTable("##Gallery", StateCount + 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
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
		Nyx::UI::TableHeadersRow();

		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();

		// Sliders get their width each: a table sets the item width per column.
		const float sliderWidth = ImGui::GetFontSize() * 10.0f;

		// One row: the widget's name, `paint` once per state, the live painted widget, and ImGui's
		const auto row = [&](const char* name, const auto& paint, const auto& live, const auto& imgui)
		{
			Nyx::UI::TableNextRow();
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

		// Buttons in a tone of their own, as Play and Stop in the titlebar
		for (const auto& [name, label, tone] : { std::tuple{ "Button, Go", "Play", Nyx::UI::EButtonTone::Go }, std::tuple{ "Button, Stop", "Stop", Nyx::UI::EButtonTone::Stop } })
		{
			ImGui::PushID(name);
			row(name,
				[&](const WidgetLook& look) { Nyx::UI::PaintButton(painter, ReserveCell(Nyx::UI::CalcButtonSize(label)), label, look, tone); },
				[&]() { NYX_UI(Nyx::UI::Button(label, glm::vec2(0.0f), tone)); },
				[&]() {});
			ImGui::PopID();
		}

		// Menu bar buttons, as File in the titlebar: closed, then with their menu open
		for (const auto& [name, open] : { std::pair{ "Menu bar button", 0.0f }, std::pair{ "Menu bar button, open", 1.0f } })
		{
			ImGui::PushID(name);
			row(name,
				[&](const WidgetLook& look) { Nyx::UI::PaintMenuBarButton(painter, ReserveCell(Nyx::UI::CalcButtonSize("File")), "File", open, look); },
				[&]()
				{
					if (NYX_UI(Nyx::UI::MenuBarButton("File", Nyx::UI::CalcButtonSize("File"), ImGui::IsPopupOpen("##GalleryMenu"))))
					{
						ImGui::OpenPopup("##GalleryMenu");
					}
					if (Nyx::UI::BeginPopup("##GalleryMenu"))
					{
						NYX_UI(Nyx::UI::MenuItem("New Scene"));
						ImGui::EndPopup();
					}
				},
				[&]() {});
			ImGui::PopID();
		}

		// The titlebar's window controls, side by side as there
		const glm::vec2 controlSize(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight() * 1.2f);
		constexpr Nyx::UI::EWindowControl controls[] = { Nyx::UI::EWindowControl::Minimize, Nyx::UI::EWindowControl::Maximize, Nyx::UI::EWindowControl::Restore,
			Nyx::UI::EWindowControl::Close };
		row("Window controls",
			[&](const WidgetLook& look)
			{
				const PainterRect cell = ReserveCell(glm::vec2(controlSize.x * static_cast<float>(std::size(controls)), controlSize.y));
				for (size_t index = 0; index < std::size(controls); ++index)
				{
					const glm::vec2 min = cell.Min + glm::vec2(controlSize.x * static_cast<float>(index), 0.0f);
					Nyx::UI::PaintWindowControl(painter, PainterRect{ min, min + controlSize }, controls[index], look);
				}
			},
			[&]()
			{
				for (size_t index = 0; index < std::size(controls); ++index)
				{
					ImGui::PushID(static_cast<int>(index));
					if (index > 0)
					{
						ImGui::SameLine(0.0f, 0.0f);
					}
					NYX_UI(Nyx::UI::WindowControlButton("##Control", controls[index], controlSize));
					ImGui::PopID();
				}
			},
			[&]() {});

		// The titlebar's icon buttons, side by side: Play, Play All, Stop, Pause and Step
		const glm::vec2 iconButtonSize = Nyx::UI::CalcIconButtonSize("##Play", Nyx::UI::EIcon::Play, false);
		constexpr std::pair<Nyx::UI::EIcon, Nyx::UI::EButtonTone> iconButtons[] = { { Nyx::UI::EIcon::Play, Nyx::UI::EButtonTone::Neutral },
			{ Nyx::UI::EIcon::PlayAll, Nyx::UI::EButtonTone::Neutral }, { Nyx::UI::EIcon::Stop, Nyx::UI::EButtonTone::Stop },
			{ Nyx::UI::EIcon::Pause, Nyx::UI::EButtonTone::Neutral }, { Nyx::UI::EIcon::Step, Nyx::UI::EButtonTone::Neutral } };
		const float iconButtonGap = ImGui::GetStyle().ItemInnerSpacing.x;
		row("Icon buttons",
			[&](const WidgetLook& look)
			{
				const PainterRect cell = ReserveCell(glm::vec2((iconButtonSize.x + iconButtonGap) * static_cast<float>(std::size(iconButtons)) - iconButtonGap, iconButtonSize.y));
				for (size_t index = 0; index < std::size(iconButtons); ++index)
				{
					const glm::vec2 min = cell.Min + glm::vec2((iconButtonSize.x + iconButtonGap) * static_cast<float>(index), 0.0f);
					Nyx::UI::PaintIconButton(painter, PainterRect{ min, min + iconButtonSize }, "", iconButtons[index].first, look, iconButtons[index].second);
				}
			},
			[&]() { NYX_UI(Nyx::UI::IconButton("##PlayLive", Nyx::UI::EIcon::Play)); },
			[&]() {});

		// An icon button with a label and a dropdown, in the Primary tone, as Build
		row("Icon button, dropdown",
			[&](const WidgetLook& look)
			{
				const glm::vec2 size = Nyx::UI::CalcIconButtonSize("Build", Nyx::UI::EIcon::Build, true);
				Nyx::UI::PaintIconButton(painter, ReserveCell(size), "Build", Nyx::UI::EIcon::Build, look, Nyx::UI::EButtonTone::Primary, true);
			},
			[&]() { NYX_UI(Nyx::UI::IconButton("Build##Live", Nyx::UI::EIcon::Build, glm::vec2(0.0f), Nyx::UI::EButtonTone::Primary, true)); },
			[&]() {});

		// Bar tabs, as for workspaces: not selected, then selected
		const float barTabHeight = ImGui::GetFrameHeight() + 6.0f;
		for (const auto& [name, selected] : { std::pair{ "Bar tab", 0.0f }, std::pair{ "Bar tab, selected", 1.0f } })
		{
			ImGui::PushID(name);
			row(name,
				[&](const WidgetLook& look) { Nyx::UI::PaintBarTab(painter, ReserveCell(Nyx::UI::CalcBarTabSize("Scene", barTabHeight)), "Scene", selected, look); },
				[&]()
				{
					if (NYX_UI(Nyx::UI::BarTab("Scene##Live", bGalleryBarTab, glm::vec2(0.0f, barTabHeight))))
					{
						bGalleryBarTab = !bGalleryBarTab;
					}
				},
				[&]() {});
			ImGui::PopID();
		}

		// A document tab, as for a scene: the close button in each state (it
		// shows only its own hover), with the tab plain, then the tab in each state
		row("Document tab, close",
			[&](const WidgetLook& look)
			{
				const glm::vec2 size = Nyx::UI::CalcDocumentTabSize("TestScene", barTabHeight);
				Nyx::UI::PaintDocumentTab(painter, ReserveCell(size), "TestScene", WidgetLook{}, look);
			},
			[&]()
			{
				NYX_UI(Nyx::UI::DocumentTab("TestScene##Live", true, glm::vec2(0.0f, barTabHeight)));
			},
			[&]() {});
		row("Document tab",
			[&](const WidgetLook& look)
			{
				const glm::vec2 size = Nyx::UI::CalcDocumentTabSize("TestScene", barTabHeight);
				Nyx::UI::PaintDocumentTab(painter, ReserveCell(size), "TestScene", look, WidgetLook{ 0.0f, 0.0f, 0.0f, true });
			},
			[&]() {},
			[&]() {});

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

		// Tree rows: a folder one level in, closed, then open and selected
		const glm::vec2 rowSize(ImGui::GetFontSize() * 9.0f, Nyx::UI::CalcTreeRowHeight());
		const auto paintTreeRow = [&](const WidgetLook& look, bool bOpenAndSelected)
		{
			const PainterRect cell = ReserveCell(rowSize);
			Nyx::UI::TreeRowLook tree;
			tree.ContentX = cell.Min.x + ImGui::GetStyle().IndentSpacing;
			tree.Depth = 1;
			tree.Open = bOpenAndSelected ? 1.0f : 0.0f;
			tree.Selected = bOpenAndSelected ? 1.0f : 0.0f;
			tree.Icon = Nyx::UI::ETreeRowIcon::Folder;
			Nyx::UI::PaintTreeRow(painter, cell, "Meshes", tree, look);
		};
		row("Tree row",
			[&](const WidgetLook& look) { paintTreeRow(look, false); },
			[&]()
			{
				// A small tree: a folder with one in it, selected by clicking
				Nyx::UI::TreeRowOptions options;
				options.bSelected = bGalleryRowSelected;
				options.Icon = Nyx::UI::ETreeRowIcon::Folder;
				const Nyx::UI::TreeRowResult tree = NYX_UI(Nyx::UI::TreeRow("Assets##Live", options));
				bGalleryRowSelected = tree.bClicked ? !bGalleryRowSelected : bGalleryRowSelected;
				if (tree.bOpen)
				{
					Nyx::UI::TreeRowOptions child;
					child.bLeaf = true;
					child.Icon = Nyx::UI::ETreeRowIcon::Folder;
					NYX_UI(Nyx::UI::TreeRow("Meshes##Live", child));
					Nyx::UI::TreeRowEnd();
				}
			},
			[&]()
			{
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				flags |= bGalleryRowSelected ? ImGuiTreeNodeFlags_Selected : 0;
				if (NYX_UI(ImGui::TreeNodeEx("Assets##ImGui", flags)))
				{
					NYX_UI(ImGui::TreeNodeEx("Meshes##ImGui", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth));
					ImGui::TreePop();
				}
			});

		row("Tree row, open, selected",
			[&](const WidgetLook& look) { paintTreeRow(look, true); },
			[&]() {},
			[&]() {});

		// Fields: ImGui draws their text, so the forced states paint a sample text over the frame.
		// Their Pressed column shows them edited.
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const float fieldWidth = ImGui::GetFontSize() * 8.0f;
		const glm::vec2 fieldSize(fieldWidth, ImGui::GetFrameHeight());
		const float textY = ImGui::GetStyle().FramePadding.y;
		const auto sampleText = [&](const WidgetLook& look, glm::vec2 position, std::string_view text, const glm::vec4& color)
		{
			painter.AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, look.bDisabled ? glm::vec4(color.r, color.g, color.b, color.a * theme.DisabledAlpha) : color, text);
		};

		row("Text field",
			[&](const WidgetLook& look)
			{
				const PainterRect frame = ReserveCell(fieldSize);
				Nyx::UI::PaintField(painter, frame, look.Pressed, look);
				sampleText(look, frame.Min + glm::vec2(ImGui::GetStyle().FramePadding.x, textY), "Cube", theme.Text);
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(Nyx::UI::InputText("##LiveText", &GalleryText));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::InputText("##ImGuiText", &GalleryText));
			});

		const glm::vec2 linesSize(fieldWidth, ImGui::GetTextLineHeight() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f);
		row("Multi-line field",
			[&](const WidgetLook& look)
			{
				const PainterRect frame = ReserveCell(linesSize);
				Nyx::UI::PaintField(painter, frame, look.Pressed, look);
				const glm::vec2 text = frame.Min + glm::vec2(ImGui::GetStyle().FramePadding.x, textY);
				sampleText(look, text, "#0 Main()", theme.Text);
				sampleText(look, text + glm::vec2(0.0f, ImGui::GetTextLineHeight()), "#1 Run()", theme.Text);
			},
			[&]() { NYX_UI(Nyx::UI::InputTextMultiline("##LiveLines", &GalleryLines, linesSize)); },
			[&]() { NYX_UI(ImGui::InputTextMultiline("##ImGuiLines", &GalleryLines, ImVec2(linesSize.x, linesSize.y))); });

		row("Search field",
			[&](const WidgetLook& look)
			{
				const PainterRect frame = ReserveCell(fieldSize);
				Nyx::UI::PaintSearchField(painter, frame, look.Pressed, look);
				sampleText(look, frame.Min + glm::vec2(Nyx::UI::CalcSearchTextOffset(), textY), "Search", theme.TextMuted);
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(Nyx::UI::SearchField("##LiveSearch", &GallerySearch));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::InputTextWithHint("##ImGuiSearch", "Search", &GallerySearch));
			});

		row("Number field",
			[&](const WidgetLook& look)
			{
				const PainterRect frame = ReserveCell(fieldSize);
				Nyx::UI::PaintField(painter, frame, look.Pressed, look);
				const float textWidth = ImGui::CalcTextSize("1.250").x;
				sampleText(look, glm::vec2(std::floor((frame.Min.x + frame.Max.x - textWidth) * 0.5f), frame.Min.y + textY), "1.250", theme.Text);
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(Nyx::UI::DragFloat("##LiveNumber", &GalleryNumber, 0.01f));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::DragFloat("##ImGuiNumber", &GalleryNumber, 0.01f));
			});

		// A vector field as wide as two fields, its components spaced as DragVector() spaces them
		const float vectorWidth = fieldWidth * 1.5f;
		row("Vector field",
			[&](const WidgetLook& look)
			{
				const PainterRect cell = ReserveCell(glm::vec2(vectorWidth, ImGui::GetFrameHeight()));
				const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
				const float componentWidth = std::floor((vectorWidth - spacing * 2.0f) / 3.0f);
				const float tagWidth = Nyx::UI::CalcAxisTagWidth();
				const char* const values[] = { "1.0", "2.0", "3.0" };
				for (int axis = 0; axis < 3; ++axis)
				{
					const float left = cell.Min.x + (componentWidth + spacing) * static_cast<float>(axis);
					const PainterRect frame{ { left, cell.Min.y }, { left + componentWidth, cell.Max.y } };
					// Only the first component shows the state, as only one is hovered or edited at a time
					const WidgetLook componentLook = axis == 0 ? look : WidgetLook{ 0.0f, 0.0f, 0.0f, look.bDisabled };
					Nyx::UI::PaintVectorComponent(painter, frame, axis, componentLook.Pressed, componentLook);
					const float textWidth = ImGui::CalcTextSize(values[axis]).x;
					sampleText(look, glm::vec2(std::floor(left + tagWidth + (componentWidth - tagWidth - textWidth) * 0.5f), cell.Min.y + textY), values[axis], theme.Text);
				}
			},
			[&]()
			{
				ImGui::SetNextItemWidth(vectorWidth);
				NYX_UI(Nyx::UI::DragVector("##LiveVector", &GalleryVector.x, 3, 0.1f, "%.1f"));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(vectorWidth);
				NYX_UI(ImGui::DragFloat3("##ImGuiVector", &GalleryVector.x, 0.1f, 0.0f, 0.0f, "%.1f"));
			});

		// A property row: the label column, and a number field in the value column. Hover is the
		// only state it has; the field shows its own.
		const float propertyRowWidth = fieldWidth * 2.0f;
		row("Property row",
			[&](const WidgetLook& look)
			{
				const PainterRect rect = ReserveCell(glm::vec2(propertyRowWidth, Nyx::UI::CalcTreeRowHeight()));
				Nyx::UI::PropertyRowLook property;
				property.ContentX = rect.Min.x;
				Nyx::UI::PaintPropertyRow(painter, rect, "Intensity", property, look);
				const float valueX = Nyx::UI::CalcPropertyValueX(rect.Min.x, rect.Max.x);
				const PainterRect frame{ { valueX, rect.Min.y }, { rect.Max.x - theme.RowPillMargin * 2.0f, rect.Max.y } };
				Nyx::UI::PaintField(painter, frame, 0.0f, WidgetLook{ 0.0f, 0.0f, 0.0f, look.bDisabled });
				const float textWidth = ImGui::CalcTextSize("1.250").x;
				sampleText(look, glm::vec2(std::floor((frame.Min.x + frame.Max.x - textWidth) * 0.5f), frame.Min.y + textY), "1.250", theme.Text);
			},
			[&]()
			{
				// A property row spans its window, so it gets a window of its own: in the table's
				// column, which fits its contents, it would take the column's width and the column
				// its own.
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
				if (ImGui::BeginChild("##LivePropertyRow", ImVec2(propertyRowWidth, Nyx::UI::CalcTreeRowHeight()), ImGuiChildFlags_None,
						ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
				{
					Nyx::UI::BeginPropertyRow("Intensity##Live");
					NYX_UI(Nyx::UI::DragFloat("##LiveValue", &GalleryNumber, 0.01f));
					Nyx::UI::EndPropertyRow();
				}
				ImGui::EndChild();
				ImGui::PopStyleVar();
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::DragFloat("Intensity##ImGui", &GalleryNumber, 0.01f));
			});

		row("Collapsing header",
			[&](const WidgetLook& look)
			{
				const PainterRect rect = ReserveCell(glm::vec2(propertyRowWidth, ImGui::GetFrameHeight()));
				Nyx::UI::PaintCollapsingHeader(painter, rect, "Transform", 1.0f, look);
			},
			[&]() { NYX_UI(Nyx::UI::CollapsingHeader("Transform##Live")); },
			[&]() { NYX_UI(ImGui::CollapsingHeader("Transform##ImGui", ImGuiTreeNodeFlags_DefaultOpen)); });

		row("Checkbox, off",
			[&](const WidgetLook& look) { Nyx::UI::PaintCheckbox(painter, ReserveCell(Nyx::UI::CalcCheckboxSize("Snap")), "Snap", 0.0f, look); },
			[&]() { NYX_UI(Nyx::UI::Checkbox("Snap##LiveCheckbox", &bGalleryToggle)); },
			[&]() { NYX_UI(ImGui::Checkbox("Snap##ImGuiCheckbox", &bGalleryToggle)); });

		row("Checkbox, on",
			[&](const WidgetLook& look) { Nyx::UI::PaintCheckbox(painter, ReserveCell(Nyx::UI::CalcCheckboxSize("Snap")), "Snap", 1.0f, look); },
			[&]() {},
			[&]() {});

		// Menu items and selectables: as wide as a small menu, a line high with the item spacing
		// around it, as ImGui lays them out. The live ones work outside a menu too.
		const ImGuiStyle& style = ImGui::GetStyle();
		const glm::vec2 itemSize(ImGui::GetFontSize() * 11.0f, ImGui::GetFontSize() + style.ItemSpacing.y);
		const auto paintMenuItem = [&](const WidgetLook& look, bool bChecked)
		{
			const PainterRect cell = ReserveCell(itemSize);
			Nyx::UI::PaintMenuItem(painter, cell, bChecked ? "Painted Chrome" : "Duplicate", bChecked ? "" : "Ctrl+D", bChecked, look);
		};
		row("Menu item",
			[&](const WidgetLook& look) { paintMenuItem(look, false); },
			[&]() { NYX_UI(Nyx::UI::MenuItem("Duplicate##Live", "Ctrl+D")); },
			[&]() { NYX_UI(ImGui::MenuItem("Duplicate##ImGui", "Ctrl+D")); });

		row("Menu item, ticked",
			[&](const WidgetLook& look) { paintMenuItem(look, true); },
			[&]() { NYX_UI(Nyx::UI::MenuItem("Painted Chrome##Live", nullptr, &bGalleryToggle)); },
			[&]() { NYX_UI(ImGui::MenuItem("Painted Chrome##ImGui", nullptr, &bGalleryToggle)); });

		row("Selectable, selected",
			[&](const WidgetLook& look)
			{
				const PainterRect cell = ReserveCell(itemSize);
				const glm::vec2 label = cell.Min + glm::vec2(std::floor(style.ItemSpacing.x * 0.5f), std::floor(style.ItemSpacing.y * 0.5f));
				Nyx::UI::PaintSelectable(painter, cell, label, "Textured Cube", 1.0f, look);
			},
			[&]()
			{
				if (NYX_UI(Nyx::UI::Selectable("Textured Cube##Live", bGalleryRowSelected)))
				{
					bGalleryRowSelected = !bGalleryRowSelected;
				}
			},
			[&]()
			{
				if (NYX_UI(ImGui::Selectable("Textured Cube##ImGui", bGalleryRowSelected)))
				{
					bGalleryRowSelected = !bGalleryRowSelected;
				}
			});

		constexpr const char* ComboItems[] = { "Visual Studio", "Visual Studio Code", "Rider" };
		const auto paintCombo = [&](const WidgetLook& look, float open)
		{
			Nyx::UI::PaintCombo(painter, ReserveCell(fieldSize), "Visual Studio", open, look);
		};
		row("Combo",
			[&](const WidgetLook& look) { paintCombo(look, 0.0f); },
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(Nyx::UI::Combo("##LiveCombo", &GalleryComboIndex, ComboItems));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::Combo("##ImGuiCombo", &GalleryComboIndex, ComboItems, static_cast<int>(std::size(ComboItems))));
			});

		row("Combo, open",
			[&](const WidgetLook& look) { paintCombo(look, 1.0f); },
			[&]() {},
			[&]() {});

		row("Colour swatch",
			[&](const WidgetLook& look) { Nyx::UI::PaintColorSwatch(painter, ReserveCell(fieldSize), GalleryColor, look); },
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(Nyx::UI::ColorSwatch("##LiveColor", &GalleryColor.x));
			},
			[&]()
			{
				ImGui::SetNextItemWidth(fieldWidth);
				NYX_UI(ImGui::ColorEdit3("##ImGuiColor", &GalleryColor.x, ImGuiColorEditFlags_NoInputs));
			});

		// Asset cards: a mesh, and a folder selected. ImGui has no cards; its column shows the
		// asset browser's list from before them.
		const auto paintAssetCard = [&](const WidgetLook& look, bool bFolder)
		{
			Nyx::UI::AssetCardLook card;
			card.Icon = bFolder ? Nyx::UI::EAssetCardIcon::Folder : Nyx::UI::EAssetCardIcon::File;
			card.TypeLabel = bFolder ? "" : "Mesh";
			card.Selected = bFolder ? 1.0f : 0.0f;
			Nyx::UI::PaintAssetCard(painter, ReserveCell(Nyx::UI::CalcAssetCardSize()), bFolder ? "Materials" : "Cube.nyxmesh", card, look);
		};
		row("Asset card",
			[&](const WidgetLook& look) { paintAssetCard(look, false); },
			[&]()
			{
				Nyx::UI::AssetCardOptions card;
				card.bSelected = bGalleryRowSelected;
				card.TypeLabel = "Mesh";
				if (NYX_UI(Nyx::UI::AssetCard("Cube.nyxmesh##Live", card)).bClicked)
				{
					bGalleryRowSelected = !bGalleryRowSelected;
				}
			},
			[&]()
			{
				if (NYX_UI(ImGui::Selectable("[Mesh] Cube.nyxmesh##ImGui", bGalleryRowSelected)))
				{
					bGalleryRowSelected = !bGalleryRowSelected;
				}
			});

		row("Asset card, folder, selected",
			[&](const WidgetLook& look) { paintAssetCard(look, true); },
			[&]() {},
			[&]() {});

		// Status bar items and the titlebar's logo have no states either: they show once, in the
		// Normal column, with a bar divider between the items, as in the status bar.
		Nyx::UI::TableNextRow();
		ImGui::TableNextColumn();
		NYX_UI(ImGui::AlignTextToFramePadding());
		NYX_UI(ImGui::TextUnformatted("Status items"));
		ImGui::TableNextColumn();
		{
			const float statusHeight = ImGui::GetFrameHeight() + 4.0f;
			const auto statusItems = { std::pair{ "0 errors", Nyx::UI::EStatusIcon::Ok }, std::pair{ "2 warnings", Nyx::UI::EStatusIcon::Warning },
				std::pair{ "1 error", Nyx::UI::EStatusIcon::Error }, std::pair{ "Vulkan", Nyx::UI::EStatusIcon::None } };
			bool bFirst = true;
			for (const auto& [text, icon] : statusItems)
			{
				if (!bFirst)
				{
					ImGui::SameLine();
					NYX_UI(Nyx::UI::BarDivider(statusHeight));
					ImGui::SameLine();
				}
				bFirst = false;
				NYX_UI(Nyx::UI::StatusItem(text, icon, statusHeight));
			}
		}

		Nyx::UI::TableNextRow();
		ImGui::TableNextColumn();
		NYX_UI(ImGui::AlignTextToFramePadding());
		NYX_UI(ImGui::TextUnformatted("Titlebar logo"));
		ImGui::TableNextColumn();
		NYX_UI(Nyx::UI::TitlebarLogo("Nyx", ImGui::GetFrameHeight() + 6.0f));

		// A section header has no states; it shows once, in the Normal column.
		Nyx::UI::TableNextRow();
		ImGui::TableNextColumn();
		NYX_UI(ImGui::AlignTextToFramePadding());
		NYX_UI(ImGui::TextUnformatted("Section header"));
		ImGui::TableNextColumn();
		Nyx::UI::PaintSectionHeader(painter, ReserveCell(glm::vec2(ImGui::GetFontSize() * 9.0f, ImGui::GetFrameHeight())), "Lighting");
		ImGui::TableSetColumnIndex(StateCount + 1);
		NYX_UI(Nyx::UI::SectionHeader("Lighting##Live"));
		ImGui::TableNextColumn();
		NYX_UI(ImGui::SeparatorText("Lighting##ImGui"));

		// A table has no states either: a small one, live, with a header, stripes and a selectable
		// row, and ImGui's beside it
		Nyx::UI::TableNextRow();
		ImGui::TableNextColumn();
		NYX_UI(ImGui::AlignTextToFramePadding());
		NYX_UI(ImGui::TextUnformatted("Table"));
		const auto smallTable = [&](bool bPainted)
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
			const glm::vec2 size(ImGui::GetFontSize() * 14.0f, 0.0f);
			if (!(bPainted ? Nyx::UI::BeginTable("##LiveTable", 3, flags, size) : ImGui::BeginTable("##ImGuiTable", 3, flags, ImVec2(size.x, size.y))))
			{
				return;
			}
			ImGui::TableSetupColumn("Time");
			ImGui::TableSetupColumn("Game");
			ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
			bPainted ? Nyx::UI::TableHeadersRow() : ImGui::TableHeadersRow();
			const char* const rows[][3] = { { "12:00:01", "Game 1", "Linked" }, { "12:00:02", "Game 2", "Linked" }, { "12:00:05", "Game 1", "Saved" }, { "12:00:09", "Game 2", "Quit" } };
			for (int index = 0; index < static_cast<int>(std::size(rows)); ++index)
			{
				bPainted ? Nyx::UI::TableNextRow() : ImGui::TableNextRow();
				ImGui::PushID(index);
				ImGui::TableNextColumn();
				const bool bSelected = index == GalleryTableRow;
				if (bPainted ? NYX_UI(Nyx::UI::Selectable(rows[index][0], bSelected, ImGuiSelectableFlags_SpanAllColumns))
							 : NYX_UI(ImGui::Selectable(rows[index][0], bSelected, ImGuiSelectableFlags_SpanAllColumns)))
				{
					GalleryTableRow = index;
				}
				ImGui::TableNextColumn();
				NYX_UI(ImGui::TextUnformatted(rows[index][1]));
				ImGui::TableNextColumn();
				NYX_UI(ImGui::TextUnformatted(rows[index][2]));
				ImGui::PopID();
			}
			bPainted ? Nyx::UI::EndTable() : ImGui::EndTable();
		};
		ImGui::TableSetColumnIndex(StateCount + 1);
		smallTable(true);
		ImGui::TableNextColumn();
		smallTable(false);

		Nyx::UI::EndTable();
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
		NYX_UI(ImGui::Text("Windows %u   Segments %u   Draws %u   Commands %u   Tiles in use %u (%u entries)   Binning %.3f ms",
			stats.Windows, stats.Segments, stats.Draws, stats.Commands, stats.TilesInUse, stats.TileEntries, stats.BinningMilliseconds));

		// Segments with nothing ImGui draws between them are one draw: per OS window
		for (const PainterWindowStats::OsWindow& osWindow : stats.OsWindows)
		{
			NYX_UI(ImGui::Text("  %s: %u segments in %u draws", osWindow.Name.c_str(), osWindow.Segments, osWindow.Draws));
		}
		if (Nyx::UI::IsMarkingImGuiShapes())
		{
			NYX_UI(ImGui::Text("ImGui draws %u shapes itself, in %u windows (Debug > Mark ImGui Shapes)", Nyx::UI::GetImGuiShapeCount(), Nyx::UI::GetImGuiShapeWindowCount()));
		}

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
		// Opened again, the statistics start over.
		FrameSamples.clear();
		NextFrameSample = 0;
	}

	void PainterTestWindow::DrawToolbar()
	{
		float zoomPercent = Zoom * 100.0f;
		ImGui::SetNextItemWidth(180.0f);
		if (NYX_UI(Nyx::UI::SliderFloat("Zoom", &zoomPercent, MinZoom * 100.0f, MaxZoom * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic)))
		{
			Zoom = std::clamp(zoomPercent / 100.0f, MinZoom, MaxZoom);
		}

		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("100%")))
		{
			Zoom = 1.0f;
			Pan = HomePan;
		}

		ImGui::SameLine();
		NYX_UI(Nyx::UI::Checkbox("Tile heatmap", &bHeatmap));
		ImGui::SameLine();
		NYX_UI(Nyx::UI::Checkbox("Compare with ImDrawList", &bCompareWithImDrawList));
		ImGui::SameLine();
		NYX_UI(ImGui::TextDisabled("Wheel zooms, drag pans"));
	}

	void PainterTestWindow::DrawStatistics() const
	{
		const std::optional<PainterTiles> tiles = Renderer ? Renderer->GetPainterCanvasTiles(CanvasId) : std::nullopt;
		if (!tiles)
		{
			NYX_UI(ImGui::TextDisabled("Not painted yet"));
			return;
		}

		// The last painted frame, as the GPU binned it; with the window open, that is the previous one.
		const PainterBinningStats& stats = tiles->Stats;
		NYX_UI(ImGui::Text("Commands %u (culled %u, dropped %u)   Groups %zu   Textures %zu",
			stats.Commands, stats.CulledCommands, stats.DroppedCommands, Painter.GetGroups().size(), Painter.GetTextures().size()));
		NYX_UI(ImGui::Text("Tiles in use %u of %u   Commands per tile in use: %.1f on average, %u at most   Entries %u (%u before the reduction)   Measuring %.3f ms",
			stats.TilesInUse, tiles->TilesX * tiles->TilesY, stats.GetAverageCommandsPerTileInUse(), stats.MaxCommandsPerTile, stats.TileEntries,
			stats.EntriesBeforeReduction, stats.BinningMilliseconds));

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

	std::vector<PainterScenario> PredictOpaqueDockedCards(const PainterCosts& now)
	{
		PainterScenario measured;
		measured.Name = "Now";
		measured.PainterCpu = now.PainterCpu;
		measured.PainterGpu = now.PainterGpu;
		measured.Frame = now.Frame;

		// What a pixel walks with the cards opaque, of what it walks now
		const float keptShare = now.Kept > 0.0f ? now.KeptCardsOpaque / now.Kept : 1.0f;
		PainterScenario opaque;
		opaque.Name = "Opaque docked cards";
		opaque.PainterCpu = now.PainterCpu;
		opaque.PainterGpu = now.PainterGpu * keptShare;
		opaque.Frame = std::max(now.Frame + (opaque.PainterGpu - now.PainterGpu), 0.0f);
		return { measured, opaque };
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
			case EPainterCommandType::Glass:
			{
				// ImDrawList can't show what lies behind; only the glass' tint, as a rounded rect
				const auto glass = GetPayload<PainterGlassPayload>(command);
				drawList.AddRectFilled(ToImVec2(glass.Rect.Min, origin), ToImVec2(glass.Rect.Max, origin), color, glass.Radius);
				break;
			}
			}

			drawList.PopClipRect();
		}
	}
}

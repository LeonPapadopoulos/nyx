#include "EditorFrame.h"
#include "FrameStats.h"

#include "ImGuiSource.h"
#include "InputPanel.h"
#include "Log.h"
#include "MaterialWorkspace.h"
#include "PlayControls.h"
#include "PlaySession.h"
#include "PreferencesWindow.h"
#include "EditorRenderer.h"
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneViewSet.h"
#include "ThemeEditorWindow.h"
#include "UIChrome.h"
#include "UIDiagnostics.h"
#include "UISourceInspector.h"
#include "UIWidgets.h"
#include "Window.h"
#include "Workspaces.h"

#if NYX_DEV_TOOLS
#include "ImGuiDebugTools.h"
#include "MockupOverlay.h"
#include "PainterTestWindow.h"
#include "SkyTablesWindow.h"
#endif

#include "imgui.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Nyx::Editor
{
	namespace
	{
		// The frame's layout: the room at the bars' ends, and the sizes of what is in them
		constexpr float BarMargin = 16.0f;
		constexpr float BarGap = 16.0f;
		constexpr float MenuButtonHeight = 24.0f;
		constexpr float MenuButtonPadding = 8.0f;
		constexpr glm::vec2 PlayButtonSize{ 34.0f, 24.0f };
		constexpr float PlayButtonSpacing = 2.0f;
		// The status bar's items, and the room on each side of the dividers between them
		constexpr float StatusGap = 20.0f;

		// A menu bar button's size: its label (without what follows "##"), with room on both sides
		glm::vec2 MenuButtonSize(const char* label)
		{
			return glm::vec2(std::floor(ImGui::CalcTextSize(label, nullptr, true).x + MenuButtonPadding * 2.0f), MenuButtonHeight);
		}

		// Opens the next menu under the menu bar button drawn last, as menus open
		void PlaceMenuUnderLastItem()
		{
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y), ImGuiCond_Appearing);
		}

		// Moves on to the next item on the line, `gap` after the last, its top at `top`
		void NextOnLine(float gap, float top)
		{
			ImGui::SameLine(0.0f, gap);
			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
		}
	}

	EditorFrame::EditorFrame(Parts parts)
		: Editor(std::move(parts))
	{
	}

	void EditorFrame::Attach(Nyx::IWindow& window, Nyx::IEditorRenderer& renderer, const Nyx::FrameStatsWindow& frameStats)
	{
		FrameStats = &frameStats;
		Window = &window;
		Renderer = &renderer;
		window.SetFrameContent(Nyx::WindowFrameContent{
			.DrawTitlebar = [this](const Nyx::WindowFrameArea& area) { DrawTitlebar(area); },
			.DrawStatusBar = [this](const Nyx::WindowFrameArea& area) { DrawStatusBar(area); },
		});
	}

	void EditorFrame::Detach()
	{
		// The frame's content calls into the editor's parts, so it must not outlive them
		Window->SetFrameContent(Nyx::WindowFrameContent{});
	}

	void EditorFrame::DrawTitlebar(const Nyx::WindowFrameArea& area)
	{
		// One row, with only what Nyx does: the logo, the menus, the open scene, and Play in the
		// middle. Each item is placed at its own top: after SameLine(), ImGui would put it at the
		// line's, the logo's.
		const float menuTop = std::floor(area.Y + (area.Height - MenuButtonHeight) * 0.5f);

		ImGui::SetCursorScreenPos(ImVec2(area.X + BarMargin, area.Y));
		NYX_UI(Nyx::UI::TitlebarLogo("", area.Height));

		// The menus' IDs end in "##Menu", apart from the scene's, which may have any name
		NextOnLine(0.0f, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton("File##Menu", MenuButtonSize("File##Menu"), ImGui::IsPopupOpen("##TitlebarFileMenu"))))
		{
			ImGui::OpenPopup("##TitlebarFileMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarFileMenu"))
		{
			DrawSceneMenuItems();
			ImGui::EndPopup();
		}

		NextOnLine(0.0f, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton("Edit##Menu", MenuButtonSize("Edit##Menu"), ImGui::IsPopupOpen("##TitlebarEditMenu"))))
		{
			ImGui::OpenPopup("##TitlebarEditMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarEditMenu"))
		{
			// What the shortcuts do (SceneEditor::HandleHotkeys())
			if (NYX_UI(Nyx::UI::MenuItem("Undo", "Ctrl+Z")))
			{
				Editor.Editing.Undo();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Redo", "Ctrl+Y")))
			{
				Editor.Editing.Redo();
			}
			Nyx::UI::Separator();
			const bool bHasSelection = Editor.Editing.Scene.GetSelection().has_value();
			if (NYX_UI(Nyx::UI::MenuItem("Copy", "Ctrl+C", false, bHasSelection)))
			{
				Editor.Editing.CopySelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Paste", "Ctrl+V", false, Editor.Editing.CanPaste())))
			{
				Editor.Editing.PasteEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Duplicate", "Ctrl+D", false, bHasSelection)))
			{
				Editor.Editing.DuplicateSelectedEntity();
			}
			ImGui::EndPopup();
		}

		NextOnLine(0.0f, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton("Window##Menu", MenuButtonSize("Window##Menu"), ImGui::IsPopupOpen("##TitlebarWindowMenu"))))
		{
			ImGui::OpenPopup("##TitlebarWindowMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarWindowMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Inspect UI source", "F8", Editor.SourceInspector.IsActive())))
			{
				Editor.SourceInspector.RequestToggle();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Preferences"))) Editor.PreferencesPanel.bOpen = true;
			// The painted widgets' colours and sizes, edited live and saved to the theme file
			NYX_UI(Nyx::UI::MenuItem("Theme", nullptr, &Editor.ThemeEditor.bOpen));
			// The material open for editing; a material opens here when double-clicked in the asset browser
			NYX_UI(Nyx::UI::MenuItem("Parameters", nullptr, &Editor.Materials.GetParameters().bOpen));
			// The material open, on a shape, lit or in its debug views
			NYX_UI(Nyx::UI::MenuItem("Preview", nullptr, &Editor.Materials.GetPreview().bOpen));
			// What the shader compiler said last, for every shader
			NYX_UI(Nyx::UI::MenuItem("Shader Log", nullptr, &Editor.Materials.GetShaderLog().bOpen));

			if (NYX_UI(Nyx::UI::MenuItem("Asset Browser")))
			{
				Editor.bAssetBrowserVisible = !Editor.bAssetBrowserVisible;
			}

			// Devices, actions and why they fire or not, contexts, the control map, events, timeline
			NYX_UI(Nyx::UI::MenuItem("Input", nullptr, &Editor.InputWindow.bOpen));

			// The game's log and the editor link's messages
			if (NYX_UI(Nyx::UI::MenuItem("Game Link", nullptr, Editor.bShowGameLink)))
			{
				Editor.bShowGameLink = !Editor.bShowGameLink;
			}

			// Window shapes for Play and Play All
			if (NYX_UI(Nyx::UI::MenuItem("Play Setups", nullptr, Editor.Play.bShowSetups)))
			{
				Editor.Play.bShowSetups = !Editor.Play.bShowSetups;
			}

			// The workspace's panels where they first were, and open
			if (NYX_UI(Nyx::UI::MenuItem("Reset Layout")))
			{
				Editor.ResetLayout();
			}

			// Another scene view, with its own camera, beside the first
			NYX_UI(Nyx::UI::Separator());
			if (NYX_UI(Nyx::UI::MenuItem("New Scene View")))
			{
				Editor.SceneViews.Open();
			}
			// Shows a scene view the way the game sees the scene: through its primary camera,
			// without editor overlays
			for (const std::unique_ptr<SceneViewPanel>& view : Editor.SceneViews.GetViews())
			{
				const std::string label = "Game View in " + view->GetTitle();
				if (NYX_UI(Nyx::UI::MenuItem(label.c_str(), nullptr, view->IsShowingGameView())))
				{
					view->SetShowsGameView(!view->IsShowingGameView());
				}
			}

			ImGui::EndPopup();
		}

		NextOnLine(0.0f, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton("Debug##Menu", MenuButtonSize("Debug##Menu"), ImGui::IsPopupOpen("##TitlebarDebugMenu"))))
		{
			ImGui::OpenPopup("##TitlebarDebugMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarDebugMenu"))
		{
#if NYX_DEV_TOOLS
			Editor.DebugTools.DrawMenu();
			Nyx::UI::Separator();
#endif
			NYX_UI(ImGui::TextDisabled("Nyx"));
#if NYX_DEV_TOOLS
			NYX_UI(Nyx::UI::MenuItem("Painter Test", nullptr, &Editor.PainterTest.bOpen));
			NYX_UI(Nyx::UI::MenuItem("Mockup Overlay", nullptr, &Editor.Mockup.bOpen));
			NYX_UI(Nyx::UI::MenuItem("Sky Tables", nullptr, &Editor.SkyTables.bOpen));
#endif
			bool bPaintedChrome = Nyx::UI::IsPaintedChromeEnabled();
			if (NYX_UI(Nyx::UI::MenuItem("Painted Chrome", nullptr, &bPaintedChrome)))
			{
				Nyx::UI::SetPaintedChrome(bPaintedChrome);
			}
			// Outlines the shapes ImGui still draws itself, to find what isn't painted yet
			bool bMarkImGuiShapes = Nyx::UI::IsMarkingImGuiShapes();
			if (NYX_UI(Nyx::UI::MenuItem("Mark ImGui Shapes", nullptr, &bMarkImGuiShapes)))
			{
				Nyx::UI::SetMarkImGuiShapes(bMarkImGuiShapes);
			}
			// What every scene view culls: the bounds it tests, green drawn and red culled; frozen,
			// each culls by where its camera was, so flying on shows what it left out
			for (const auto& [label, mode] : { std::pair{ "Culling Bounds", Nyx::ESceneViewCullingDebug::Bounds },
					 std::pair{ "Freeze Culling", Nyx::ESceneViewCullingDebug::Frozen } })
			{
				const Nyx::ESceneViewCullingDebug culling = Editor.SceneViews.GetCullingDebug();
				if (NYX_UI(Nyx::UI::MenuItem(label, nullptr, culling == mode)))
				{
					Editor.SceneViews.SetCullingDebug(culling == mode ? Nyx::ESceneViewCullingDebug::Off : mode);
				}
			}
			// Every scene view's shadows: tinted by the cascade each surface's shadow comes from, and
			// the cascades' shadow maps along the bottom
			const bool bShadowCascades = Editor.SceneViews.GetShadowDebug() != Nyx::ESceneViewShadowDebug::Off;
			if (NYX_UI(Nyx::UI::MenuItem("Shadow Cascades", nullptr, bShadowCascades)))
			{
				Editor.SceneViews.SetShadowDebug(bShadowCascades ? Nyx::ESceneViewShadowDebug::Off : Nyx::ESceneViewShadowDebug::Cascades);
			}
			// Every scene view's bloom alone, as the tonemap adds it, or its ambient occlusion
			for (const auto& [label, view] : { std::pair{ "Bloom", Nyx::ESceneViewDebug::Bloom }, std::pair{ "Ambient Occlusion", Nyx::ESceneViewDebug::AmbientOcclusion } })
			{
				const bool bShown = Editor.SceneViews.GetViewDebug() == view;
				if (NYX_UI(Nyx::UI::MenuItem(label, nullptr, bShown)))
				{
					Editor.SceneViews.SetViewDebug(bShown ? Nyx::ESceneViewDebug::Lit : view);
				}
			}
			// Where the painter's work lies: one of the two views, or neither
			const Nyx::UI::EPainterHeatmap heatmap = Nyx::UI::GetPainterHeatmap();
			for (const auto& [label, view] : { std::pair{ "Painter Heatmap: Commands", Nyx::UI::EPainterHeatmap::Commands },
					 std::pair{ "Painter Heatmap: Draws", Nyx::UI::EPainterHeatmap::Draws } })
			{
				if (NYX_UI(Nyx::UI::MenuItem(label, nullptr, heatmap == view)))
				{
					Nyx::UI::SetPainterHeatmap(heatmap == view ? Nyx::UI::EPainterHeatmap::Off : view);
				}
			}
			ImGui::EndPopup();
		}

		// The workspaces, between dividers
		NextOnLine(BarGap * 0.5f, area.Y);
		NYX_UI(Nyx::UI::BarDivider(area.Height));
		NextOnLine(BarGap * 0.5f, area.Y);
		DrawWorkspaceTabs(area);

		NextOnLine(BarGap * 0.5f, area.Y);
		NYX_UI(Nyx::UI::BarDivider(area.Height));

		// The open scene, whose menu makes, saves and loads scenes
		const std::string sceneName = Editor.CurrentScenePath.empty() ? std::string("Untitled Scene") : Editor.CurrentScenePath.stem().string();
		const std::string sceneLabel = sceneName + "##SceneMenu";
		NextOnLine(BarGap * 0.5f - MenuButtonPadding, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton(sceneLabel.c_str(), MenuButtonSize(sceneLabel.c_str()), ImGui::IsPopupOpen("##TitlebarSceneMenu"))))
		{
			ImGui::OpenPopup("##TitlebarSceneMenu");
		}
		ImGui::SetItemTooltip("%s", Editor.CurrentScenePath.empty() ? "Not saved yet" : Editor.CurrentScenePath.string().c_str());
		const float sceneRight = ImGui::GetItemRectMax().x;
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarSceneMenu"))
		{
			DrawSceneMenuItems();
			ImGui::EndPopup();
		}

		// Play and the buttons beside it in the middle of the titlebar, unless the scene's name reaches there
		const float playButtonsWidth = PlayControls::GetButtonsWidth(PlayButtonSize, PlayButtonSpacing);
		const float playLeft = std::max(std::floor(area.X + (area.Width - playButtonsWidth) * 0.5f), sceneRight + BarGap);
		ImGui::SetCursorScreenPos(ImVec2(playLeft, std::floor(area.Y + (area.Height - PlayButtonSize.y) * 0.5f)));
		Editor.Play.DrawButtons(Editor.Games, Editor.Preferences, PlayButtonSize, PlayButtonSpacing);
	}

	void EditorFrame::DrawSceneMenuItems()
	{
		if (NYX_UI(Nyx::UI::MenuItem("New Scene")))
		{
			Editor.NewScene();
		}

		if (NYX_UI(Nyx::UI::MenuItem("Save Scene")))
		{
			Editor.SaveScene();
		}

		if (NYX_UI(Nyx::UI::MenuItem("Save Scene As")))
		{
			Editor.SceneFiles.RequestSaveAs();
		}

		if (NYX_UI(Nyx::UI::MenuItem("Load Scene")))
		{
			Editor.SceneFiles.RequestLoad();
		}
	}

	void EditorFrame::DrawStatusBar(const Nyx::WindowFrameArea& area)
	{
		// At the left: what F8 does while it inspects, or else the last scene saved or opened
		ImGui::SetCursorScreenPos(ImVec2(area.X + BarMargin, area.Y));
		if (Editor.SourceInspector.IsActive())
		{
			NYX_UI(Nyx::UI::StatusItem("Inspect UI: click source, Shift-click widget, Esc cancels", Nyx::UI::EStatusIcon::None, area.Height));
		}
		else if (!SceneFileStatus.empty())
		{
			NYX_UI(Nyx::UI::StatusItem(SceneFileStatus.c_str(), bSceneFileStatusFailed ? Nyx::UI::EStatusIcon::Error : Nyx::UI::EStatusIcon::Ok, area.Height));
		}

		// At the right: the errors logged, the graphics API, and the frame rate and time: the last
		// second's average, ImGui's until the first second has passed
		const Nyx::LogCounts counts = Nyx::Logger::Get().GetCounts();
		char errors[32];
		std::snprintf(errors, sizeof(errors), "%llu %s", static_cast<unsigned long long>(counts.Errors), counts.Errors == 1 ? "error" : "errors");
		const bool bFrameStats = FrameStats && FrameStats->GetWindowFrames() > 0;
		const float milliseconds = bFrameStats ? static_cast<float>(FrameStats->GetAverage()[Nyx::EFrameStat::FrameTime])
			: ImGui::GetIO().Framerate > 0.0f        ? 1000.0f / ImGui::GetIO().Framerate
													 : 0.0f;
		char frameRate[32];
		std::snprintf(frameRate, sizeof(frameRate), "%.0f FPS", milliseconds > 0.0f ? 1000.0f / milliseconds : 0.0f);
		char frameTime[32];
		std::snprintf(frameTime, sizeof(frameTime), "%.1f ms", milliseconds);

		struct Item
		{
			const char* Text;
			Nyx::UI::EStatusIcon Icon;
		};
		const Item items[] = {
			{ errors, counts.Errors == 0 ? Nyx::UI::EStatusIcon::Ok : Nyx::UI::EStatusIcon::Error },
			{ Renderer->GetApiName(), Nyx::UI::EStatusIcon::None },
			{ frameRate, Nyx::UI::EStatusIcon::None },
			{ frameTime, Nyx::UI::EStatusIcon::None },
		};
		float width = 0.0f;
		for (const Item& item : items)
		{
			width += Nyx::UI::CalcStatusItemSize(item.Text, item.Icon, area.Height).x;
		}
		width += (std::size(items) - 1) * (1.0f + StatusGap * 2.0f);

		ImGui::SetCursorScreenPos(ImVec2(std::floor(area.X + area.Width - BarMargin - width), area.Y));
		for (size_t index = 0; index < std::size(items); ++index)
		{
			if (index > 0)
			{
				NextOnLine(StatusGap, area.Y);
				NYX_UI(Nyx::UI::BarDivider(area.Height));
				NextOnLine(StatusGap, area.Y);
			}
			NYX_UI(Nyx::UI::StatusItem(items[index].Text, items[index].Icon, area.Height));
			if (index == 0)
			{
				ImGui::SetItemTooltip("Errors the editor has logged since it started, and %llu warnings. Its console shows them.",
					static_cast<unsigned long long>(counts.Warnings));
			}
			if (index == std::size(items) - 1 && bFrameStats)
			{
				DrawFrameTimeTooltip();
			}
		}
	}

	void EditorFrame::DrawFrameTimeTooltip()
	{
		if (!ImGui::BeginItemTooltip())
		{
			return;
		}
		using Nyx::EFrameStat;
		const Nyx::FrameStatsSample& average = FrameStats->GetAverage();
		const Nyx::FrameStatsSample& worst = FrameStats->GetWorst();
		ImGui::Text("The last second: %.2f ms a frame on average, %.2f ms the slowest", average[EFrameStat::FrameTime], worst[EFrameStat::FrameTime]);
		ImGui::Text("CPU: update %.2f, simulation %.2f, extract %.2f, cull %.2f, record %.2f, UI %.2f, waiting for the GPU %.2f",
			average[EFrameStat::Update], average[EFrameStat::Simulation], average[EFrameStat::Extract], average[EFrameStat::Cull], average[EFrameStat::Record],
			average[EFrameStat::UI], average[EFrameStat::WaitForGpu]);
		ImGui::Text("GPU: %.2f ms; the scene views' sky %.2f, shadows %.2f, scenes %.2f, ambient occlusion %.2f, particles %.2f, bloom %.2f, pictures %.2f, grading %.2f",
			average[EFrameStat::GpuFrame], average[EFrameStat::GpuSky], average[EFrameStat::GpuShadows], average[EFrameStat::GpuScene], average[EFrameStat::GpuAmbientOcclusion],
			average[EFrameStat::GpuParticles], average[EFrameStat::GpuBloom], average[EFrameStat::GpuPicture], average[EFrameStat::GpuColorGrading]);
		ImGui::Text("%.0f scene views at %.0fx MSAA: %.0f draw calls, %.0f instances, %.0f culled, %.0f triangles; shadows %.0f draw calls",
			average[EFrameStat::SceneViews], average[EFrameStat::MsaaSamples], average[EFrameStat::DrawCalls], average[EFrameStat::Instances],
			average[EFrameStat::InstancesCulled], average[EFrameStat::Triangles], average[EFrameStat::ShadowDrawCalls]);
		ImGui::Text("Point lights %.0f, particles %.0f from %.0f emitters", average[EFrameStat::PointLights], average[EFrameStat::Particles],
			average[EFrameStat::ParticleEmitters]);
		ImGui::EndTooltip();
	}

	void EditorFrame::SetSceneFileStatus(std::string text, bool bFailed)
	{
		SceneFileStatus = std::move(text);
		bSceneFileStatusFailed = bFailed;
	}

	void EditorFrame::DrawWorkspaceTabs(const Nyx::WindowFrameArea& area)
	{
		for (const EWorkspace workspace : { EWorkspace::Scene, EWorkspace::Materials })
		{
			NextOnLine(0.0f, area.Y);
			const std::string label = std::string(GetWorkspaceName(workspace)) + "##Workspace";
			if (NYX_UI(Nyx::UI::BarTab(label.c_str(), Editor.Layouts.GetCurrent() == workspace, glm::vec2(0.0f, area.Height))))
			{
				Editor.Layouts.Request(workspace);
			}
			ImGui::SetItemTooltip(workspace == EWorkspace::Scene ? "The scene: outliner, scene views, details" : "Materials: preview, parameters, Shader Log");
		}
	}
}

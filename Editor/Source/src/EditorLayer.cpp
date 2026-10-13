#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "DefaultScene.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectionSourceRegistry.h"
#include "Renderer.h"
#include "SceneSerializer.h"
#include "UIChrome.h"
#include "UIDiagnostics.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <utility>

namespace Nyx::Editor
{
	namespace
	{
		// The scene views' free cameras, which the renderer holds
		class RendererEditorCameras final : public IEditorCameras
		{
		public:
			explicit RendererEditorCameras(Nyx::IRenderer& renderer)
				: Renderer(renderer)
			{
			}

			bool GetCamera(uint64_t sceneViewId, glm::vec3& outPosition, glm::vec3& outRotationRadians) const override
			{
				return Renderer.GetSceneViewEditorCameraTransform(sceneViewId, outPosition, outRotationRadians);
			}

			void SetCamera(uint64_t sceneViewId, const glm::vec3& position, const glm::vec3& rotationRadians) override
			{
				Renderer.SetSceneViewEditorCameraTransform(sceneViewId, position, rotationRadians);
			}

		private:
			Nyx::IRenderer& Renderer;
		};

		// The frame's layout, after the reference mockup: the room at the bars' ends, and the
		// sizes of what is in them
		constexpr float BarMargin = 16.0f;
		constexpr float BarGap = 16.0f;
		constexpr float ProjectComboWidth = 122.0f;
		constexpr float ControlHeight = 28.0f;
		constexpr float DocumentTabMinWidth = 180.0f;
		constexpr float MenuButtonHeight = 30.0f;
		constexpr float MenuButtonPadding = 11.0f;
		constexpr float CommandSearchWidth = 290.0f;
		constexpr glm::vec2 PlayButtonSize{ 46.0f, 29.0f };
		constexpr float PlayButtonSpacing = 4.0f;
		// Tabs stand this far below the titlebar's top
		constexpr float TabTop = 4.0f;
		// The status bar's items, and the room on each side of the dividers between them
		constexpr float StatusGap = 20.0f;

		// A tooltip for a control that waits for what Nyx doesn't do yet; shown while it is disabled
		void NotYetTooltip(const char* what)
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("%s: not in Nyx yet", what);
			}
		}

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

		// The frame padding that makes ImGui's frames `height` tall at the current font
		ImVec2 FramePaddingFor(float height)
		{
			return ImVec2(ImGui::GetStyle().FramePadding.x, std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f));
		}

		// The time of day, as "21:58"
		std::string TimeOfDay()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{};
			localtime_s(&local, &now);
			char text[16];
			std::strftime(text, sizeof(text), "%H:%M", &local);
			return text;
		}
	}

	void EditorLayer::OnAttach(Nyx::Engine::Application& application)
	{
		Window = &application.GetWindow();
		Renderer = &Window->GetRenderer();
		Input = &application.GetInput();
		AssetResolver = std::make_unique<Nyx::Engine::BuiltinAssetResolver>(*Renderer);
		if (!Preferences.Load(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Could not read editor preferences; using defaults.");
		}
		ApplyFrameRate();
		RegisterRuntimeReflectedSources();
		SourceInspector.Attach(
			[this](SourceLocation location)
			{
				PreferencesPanel.OpenSource(location, Preferences.SourceEditor);
			});

		// The editor's theme from its file, before anything is drawn
		ThemeEditor.LoadAtStartup(Preferences);

		Window->SetStartupStatus("Preparing the scene and editor panels");
		// The editor's windows are painted as cards on a backdrop (Debug > Painted Chrome)
		Nyx::UI::SetPaintedChrome(true);
		Window->SetFrameContent(Nyx::WindowFrameContent{
			.DrawTitlebar = [this](const Nyx::WindowFrameArea& area) { DrawTitlebar(area); },
			.DrawToolbar = [this](const Nyx::WindowFrameArea& area) { DrawToolbar(area); },
			.DrawStatusBar = [this](const Nyx::WindowFrameArea& area) { DrawStatusBar(area); },
		});

		Renderer->SetWorld(&Editing.Scene.GetRegistry());

		{
			std::filesystem::create_directories(Nyx::Paths::GetAssetsDir());
			std::filesystem::create_directories(Nyx::Paths::GetScenesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMeshesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMaterialsDir());
			std::filesystem::create_directories(Nyx::Paths::GetTexturesDir());

			AssetDb.SetAssetRoot(Nyx::Paths::GetAssetsDir());
			AssetDb.Rescan();

			AssetActivationContext = std::make_unique<EditorAssetActivationContext>(*this);
			AssetBrowser.SetActivationContext(AssetActivationContext.get());
			AssetBrowser.SetDatabase(&AssetDb);
			AssetBrowser.SetCurrentDirectory(std::filesystem::path("Scenes"));
		}

		RegisterDefaultPropertyWidgets();

		MainSceneView.CreateView(*Renderer);
		SecondarySceneView.CreateView(*Renderer);

		{
			// @todo: Allow the user to switch Camera Modes on demand via editor UI and on per-view basis
			const uint64_t secondaryViewId = SecondarySceneView.GetViewId();
			Renderer->SetSceneViewCameraMode(secondaryViewId, EViewportCameraMode::EditorFreeCamera);
			// Give the 2nd camera a different starting position & rotation to make it distinctly different
			glm::vec3 camPosition = glm::vec3(6.0f, 2.0f, 0.0f);
			glm::vec3 camRotationRadians = glm::vec3(-0.25f, 0.5 * 3.1415 /* PI */, 0.0f);
			Renderer->SetSceneViewEditorCameraTransform(secondaryViewId, camPosition, camRotationRadians);
		}

		SpawnDefaultScene(Editing.Scene);
		ResolveSceneRuntimeAssets();

		{
			const std::filesystem::path testScenePath = "TestScene.nyxscene";

			if (SaveCurrentScene(testScenePath))
			{
				const bool bLoaded = LoadCurrentScene(testScenePath);
				ASSERT(bLoaded && "Scene round-trip load failed.");
			}
		}

		// Edits, undo and redo load the assets they need, as loading a scene does
		Editing.AssetLoader.SetPostLoadContext(Nyx::Engine::ScenePostLoadContext{ .AssetResolver = AssetResolver.get() });
		Editing.Transactions.Subscribe(&Editing.AssetLoader);

		// Edits show up live in the game started with Play
		Editing.Transactions.Subscribe(&Games.GetEditSubscriber());
	}

	void EditorLayer::OnDetach()
	{
		// Closing the editor also ends the games it started
		Games.StopBeforeEditorCloses();

		SourceInspector.Detach();
		// The frame's content calls into this layer, so it must not outlive it.
		Window->SetFrameContent(Nyx::WindowFrameContent{});

		MainSceneView.DestroyView();
		SecondarySceneView.DestroyView();
		PainterTest.Release();
		Mockup.Release();
	}

	void EditorLayer::OnUpdate(float deltaTime)
	{
		DebugTools.BeforeFrame();

		RendererEditorCameras cameras(*Renderer);
		const EditorCameraController::Blockers cameraBlockers{
			.bInspectingSources = SourceInspector.IsActive(),
			.bTypingText = ImGui::GetIO().WantTextInput,
		};
		CameraController.Update(*Input, deltaTime, cameraBlockers, cameras);

		Games.Update();

		// A game that crashed gets a crash card, with its last log lines
		for (GameCrash& crash : Games.TakeCrashes())
		{
			std::string lastLogLines = GameLinkWindow.GetLastLogLines(crash.GameName, CrashCardsPanel::LogLineCount);
			CrashCards.Add(std::move(crash), std::move(lastLogLines));
		}

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(Editing.Scene.GetSelection());
	}

	void EditorLayer::OnUI()
	{
		if (!SourceInspector.IsActive()) Editing.Gizmo.TickHotkeys();

		ApplyPendingPickResults();

		const SceneFileRequest sceneFileRequest = SceneFiles.Draw(AssetDb);
		if (sceneFileRequest.Load)
		{
			LoadCurrentScene(*sceneFileRequest.Load);
		}
		if (sceneFileRequest.SaveAs)
		{
			SaveSceneAs(*sceneFileRequest.SaveAs);
		}

		if (bAssetBrowserVisible)
		{
			AssetBrowser.Draw();
		}

		Outliner.Draw(Editing);
		Details.Draw(Editing);
		MainSceneView.Draw(Editing, Preferences, CameraController);
		SecondarySceneView.Draw(Editing, Preferences, CameraController);

		// Values being dragged or typed reach a running game every frame, not only when the edit ends
		Editing.DetailsContext.PreviewPendingEdits();
		Editing.Gizmo.PreviewActiveEdit(Editing.Transactions);

		if (!SourceInspector.IsActive()) Editing.HandleHotkeys();
		PreferencesPanel.Draw(Preferences, SourceInspector);
		// The theme window remembers the theme loaded in the preferences
		const std::string theme = Preferences.Theme;
		ThemeEditor.Draw(Preferences);
		if (Preferences.Theme != theme && !Preferences.Save(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Couldn't save the editor preferences to '{0}'", EditorPreferences::GetUserFile().string());
		}
		ApplyFrameRate();

		if (bShowGameLink)
		{
			GameLinkWindow.Draw(bShowGameLink);
		}

		Play.DrawSetupsWindow(Preferences);

		if (const std::optional<GameCrash> restart = CrashCards.Draw())
		{
			Games.Restart(*restart);
		}

		DebugTools.DrawWindows();
		PainterTest.SetFrameRateLimits(Window->IsVSync(), Window->GetFrameRateCap());
		PainterTest.Draw(*Renderer);
		Mockup.Draw(*Renderer);
	}

	void EditorLayer::ApplyFrameRate()
	{
		// The window acts only on a change: a new swapchain for vsync, a new pace for the cap.
		Window->SetVSync(Preferences.bVSync);
		Window->SetFrameRateCap(Preferences.FrameRateCap);
	}

	void EditorLayer::DrawTitlebar(const Nyx::WindowFrameArea& area)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, FramePaddingFor(ControlHeight - 1.0f));
		const float controlTop = std::floor(area.Y + (area.Height - ImGui::GetFrameHeight()) * 0.5f);

		ImGui::SetCursorScreenPos(ImVec2(area.X + BarMargin, area.Y));
		NYX_UI(Nyx::UI::TitlebarLogo("Nyx", area.Height));
		NextOnLine(BarGap * 0.75f, area.Y);
		NYX_UI(Nyx::UI::BarDivider(area.Height));

		// The project, for when Nyx has projects
		NextOnLine(BarGap, controlTop);
		ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(ProjectComboWidth);
		if (Nyx::UI::BeginCombo("##Project", "Project"))
		{
			Nyx::UI::EndCombo();
		}
		NotYetTooltip("Projects");
		ImGui::EndDisabled();

		// The open scene's tab. There is one scene at a time, so it doesn't close.
		const std::string sceneName = CurrentScenePath.empty() ? std::string("Untitled Scene") : CurrentScenePath.stem().string();
		const float tabHeight = area.Height - TabTop;
		const float tabWidth = std::max(Nyx::UI::CalcDocumentTabSize(sceneName.c_str(), tabHeight).x, DocumentTabMinWidth);
		NextOnLine(BarGap, area.Y + TabTop);
		NYX_UI(Nyx::UI::DocumentTab((sceneName + "##SceneTab").c_str(), false, glm::vec2(tabWidth, tabHeight)));
		ImGui::SetItemTooltip("%s", CurrentScenePath.empty() ? "Not saved yet" : CurrentScenePath.string().c_str());

		NextOnLine(BarGap, area.Y);
		NYX_UI(Nyx::UI::BarDivider(area.Height));

		// The workspaces: Scene, the only one so far, and the others to come. Their IDs differ from
		// the menus' of the same names (Scene, Debug), which lie in the same window.
		const char* const workspaces[] = { "Scene##Workspace", "Materials##Workspace", "Animation##Workspace", "UI##Workspace", "Logic##Workspace",
			"Debug##Workspace" };
		for (size_t index = 0; index < std::size(workspaces); ++index)
		{
			NextOnLine(index == 0 ? BarGap : 0.0f, area.Y + TabTop - 1.0f);
			ImGui::BeginDisabled(index > 0);
			NYX_UI(Nyx::UI::BarTab(workspaces[index], index == 0, glm::vec2(0.0f, area.Height - TabTop * 2.0f + 1.0f)));
			if (index > 0)
			{
				NotYetTooltip("Workspaces");
			}
			ImGui::EndDisabled();
		}

		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawToolbar(const Nyx::WindowFrameArea& area)
	{
		const float menuTop = std::floor(area.Y + (area.Height - MenuButtonHeight) * 0.5f);
		ImGui::SetCursorScreenPos(ImVec2(area.X + BarMargin - MenuButtonPadding, menuTop));

		// The menus' IDs end in "##Menu", apart from the workspaces' of the same names in the titlebar
		if (NYX_UI(Nyx::UI::MenuBarButton("File##Menu", MenuButtonSize("File##Menu"), ImGui::IsPopupOpen("##TitlebarFileMenu"))))
		{
			ImGui::OpenPopup("##TitlebarFileMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarFileMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("New Scene")))
			{
				NewScene();
			}

			if (NYX_UI(Nyx::UI::MenuItem("Save Scene")))
			{
				SaveScene();
			}

			if (NYX_UI(Nyx::UI::MenuItem("Save Scene As")))
			{
				SceneFiles.RequestSaveAs();
			}

			if (NYX_UI(Nyx::UI::MenuItem("Load Scene")))
			{
				SceneFiles.RequestLoad();
			}

			ImGui::EndPopup();
		}

		// Menus for what Nyx doesn't have yet
		ImGui::BeginDisabled();
		for (const auto& [menu, tooltip] : { std::pair{ "Edit##Menu", "The Edit menu (undo, redo, copy and paste have their shortcuts)" },
				 std::pair{ "Scene##Menu", "The Scene menu" }, std::pair{ "Tools##Menu", "The Tools menu" } })
		{
			ImGui::SameLine(0.0f, 0.0f);
			NYX_UI(Nyx::UI::MenuBarButton(menu, MenuButtonSize(menu), false));
			NotYetTooltip(tooltip);
		}
		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, 0.0f);
		if (NYX_UI(Nyx::UI::MenuBarButton("Window##Menu", MenuButtonSize("Window##Menu"), ImGui::IsPopupOpen("##TitlebarWindowMenu"))))
		{
			ImGui::OpenPopup("##TitlebarWindowMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarWindowMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Inspect UI source", "F8", SourceInspector.IsActive())))
			{
				SourceInspector.RequestToggle();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Preferences"))) PreferencesPanel.bOpen = true;
			// The painted widgets' colours and sizes, edited live and saved to the theme file
			NYX_UI(Nyx::UI::MenuItem("Theme", nullptr, &ThemeEditor.bOpen));

			if (NYX_UI(Nyx::UI::MenuItem("Asset Browser")))
			{
				bAssetBrowserVisible = !bAssetBrowserVisible;
			}

			// The game's log and the editor link's messages
			if (NYX_UI(Nyx::UI::MenuItem("Game Link", nullptr, bShowGameLink)))
			{
				bShowGameLink = !bShowGameLink;
			}

			// Window shapes for Play and Play All
			if (NYX_UI(Nyx::UI::MenuItem("Play Setups", nullptr, Play.bShowSetups)))
			{
				Play.bShowSetups = !Play.bShowSetups;
			}

			// Shows "Scene 2" the way the game sees the scene: through its primary camera, without editor overlays
			if (NYX_UI(Nyx::UI::MenuItem("Game View in Scene 2", nullptr, bSecondaryViewShowsGameView)))
			{
				bSecondaryViewShowsGameView = !bSecondaryViewShowsGameView;
				SecondarySceneView.SetShowsGameView(bSecondaryViewShowsGameView);
			}

			ImGui::EndPopup();
		}

		ImGui::SameLine(0.0f, 0.0f);
		if (NYX_UI(Nyx::UI::MenuBarButton("Debug##Menu", MenuButtonSize("Debug##Menu"), ImGui::IsPopupOpen("##TitlebarDebugMenu"))))
		{
			ImGui::OpenPopup("##TitlebarDebugMenu");
		}
		PlaceMenuUnderLastItem();
		if (Nyx::UI::BeginPopup("##TitlebarDebugMenu"))
		{
			DebugTools.DrawMenu();
			Nyx::UI::Separator();
			NYX_UI(ImGui::TextDisabled("Nyx"));
			NYX_UI(Nyx::UI::MenuItem("Painter Test", nullptr, &PainterTest.bOpen));
			NYX_UI(Nyx::UI::MenuItem("Mockup Overlay", nullptr, &Mockup.bOpen));
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

		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginDisabled();
		NYX_UI(Nyx::UI::MenuBarButton("Help##Menu", MenuButtonSize("Help##Menu"), false));
		NotYetTooltip("Help");
		ImGui::EndDisabled();

		NextOnLine(BarGap * 0.5f, area.Y);
		NYX_UI(Nyx::UI::BarDivider(area.Height));

		// The command search, for when there are commands to search
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, FramePaddingFor(ControlHeight));
		NextOnLine(BarGap * 0.75f, std::floor(area.Y + (area.Height - ImGui::GetFrameHeight()) * 0.5f));
		ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(CommandSearchWidth);
		NYX_UI(Nyx::UI::SearchField("##CommandSearch", &CommandSearchText, "Search commands..."));
		NotYetTooltip("Command search");
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		const float searchRight = ImGui::GetItemRectMax().x;

		// Play and the buttons beside it in the middle of the window, unless the menus reach there
		const float playButtonsWidth = PlayControls::GetButtonsWidth(PlayButtonSize, PlayButtonSpacing);
		const float playLeft = std::max(std::floor(area.X + (area.Width - playButtonsWidth) * 0.5f), searchRight + BarGap);
		const float buttonTop = std::floor(area.Y + (area.Height - PlayButtonSize.y) * 0.5f);
		ImGui::SetCursorScreenPos(ImVec2(playLeft, buttonTop));
		Play.DrawButtons(Games, Preferences, PlayButtonSize, PlayButtonSpacing);

		// Build at the right end, for when Nyx builds games
		const glm::vec2 buildSize = Nyx::UI::CalcIconButtonSize("Build", Nyx::UI::EIcon::Build, true, PlayButtonSize.y);
		const float buildLeft = std::max(area.X + area.Width - BarMargin - buildSize.x, playLeft + playButtonsWidth + BarGap);
		ImGui::SetCursorScreenPos(ImVec2(buildLeft, buttonTop));
		ImGui::BeginDisabled();
		NYX_UI(Nyx::UI::IconButton("Build", Nyx::UI::EIcon::Build, buildSize, Nyx::UI::EButtonTone::Primary, true));
		NotYetTooltip("Building games");
		ImGui::EndDisabled();
	}

	void EditorLayer::DrawStatusBar(const Nyx::WindowFrameArea& area)
	{
		// At the left: what F8 does while it inspects, or else the last scene saved or opened
		ImGui::SetCursorScreenPos(ImVec2(area.X + BarMargin, area.Y));
		if (SourceInspector.IsActive())
		{
			NYX_UI(Nyx::UI::StatusItem("Inspect UI: click source, Shift-click widget, Esc cancels", Nyx::UI::EStatusIcon::None, area.Height));
		}
		else if (!SceneFileStatus.empty())
		{
			NYX_UI(Nyx::UI::StatusItem(SceneFileStatus.c_str(), bSceneFileStatusFailed ? Nyx::UI::EStatusIcon::Error : Nyx::UI::EStatusIcon::Ok, area.Height));
		}

		// At the right: the errors logged, the graphics API, and the frame rate and time
		const Nyx::Core::LogCounts counts = Nyx::Core::Logger::Get().GetCounts();
		char errors[32];
		std::snprintf(errors, sizeof(errors), "%llu %s", static_cast<unsigned long long>(counts.Errors), counts.Errors == 1 ? "error" : "errors");
		const float framesPerSecond = ImGui::GetIO().Framerate;
		char frameRate[32];
		std::snprintf(frameRate, sizeof(frameRate), "%.0f FPS", framesPerSecond);
		char frameTime[32];
		std::snprintf(frameTime, sizeof(frameTime), "%.1f ms", framesPerSecond > 0.0f ? 1000.0f / framesPerSecond : 0.0f);

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
		}
	}

	void EditorLayer::SetSceneFileStatus(std::string text, bool bFailed)
	{
		SceneFileStatus = std::move(text);
		bSceneFileStatusFailed = bFailed;
	}

	bool EditorLayer::SaveCurrentScene(const std::filesystem::path& path)
	{
		if (!Nyx::Engine::SceneSerializer::SaveToFile(Editing.Scene.GetRegistry(), path))
		{
			LOG_ERROR("Failed to save scene to '{0}'", path.string());
			SetSceneFileStatus("Couldn't save " + path.filename().string(), true);
			return false;
		}

		LOG_INFO("Saved scene to '{0}'", path.string());
		SetSceneFileStatus("Saved " + path.filename().string() + " at " + TimeOfDay(), false);
		return true;
	}

	bool EditorLayer::LoadCurrentScene(const std::filesystem::path& path)
	{
		Nyx::Engine::ScenePostLoadContext postLoadContext{};
		postLoadContext.AssetResolver = AssetResolver.get();

		// Keep the current scene and its pending edits until the replacement is ready.
		Nyx::Engine::Registry loadedWorld;
		if (!Nyx::Engine::SceneSerializer::LoadFromFile(path, loadedWorld, postLoadContext))
		{
			LOG_ERROR("Failed to load scene from '{0}'", path.string());
			SetSceneFileStatus("Couldn't open " + path.filename().string(), true);
			return false;
		}

		ForgetPreviousScene();
		Editing.Scene.GetRegistry() = std::move(loadedWorld);
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath = path;

		LOG_INFO("Loaded scene from '{0}'", path.string());
		SetSceneFileStatus("Opened " + path.filename().string() + " at " + TimeOfDay(), false);
		return true;
	}

	bool EditorLayer::SaveScene()
	{
		if (CurrentScenePath.empty())
		{
			SceneFiles.RequestSaveAs();
			return false;
		}

		return SaveCurrentScene(CurrentScenePath);
	}

	bool EditorLayer::SaveSceneAs(const std::filesystem::path& path)
	{
		if (SaveCurrentScene(path))
		{
			CurrentScenePath = path;
			AssetDb.Rescan();
			return true;
		}

		return false;
	}

	bool EditorLayer::NewScene()
	{
		ForgetPreviousScene();
		Editing.Scene.GetRegistry().Clear();
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath.clear();
		SetSceneFileStatus("New scene, not saved yet", false);
		return true;
	}

	void EditorLayer::ForgetPreviousScene()
	{
		Editing.ForgetScene();

		// The games are still running the previous scene
		Games.OnSceneReplaced();
	}

	void EditorLayer::ResolveSceneRuntimeAssets()
	{
		Editing.Scene.GetRegistry().Each<Nyx::Engine::MeshRendererComponent>(
			[this](Nyx::Engine::Entity /*entity*/, Nyx::Engine::MeshRendererComponent& component)
			{
				component.MeshAsset =
					component.Mesh.IsValid() ? AssetResolver->ResolveMesh(component.Mesh.Path) : nullptr;

				component.MaterialAsset =
					component.Material.IsValid() ? AssetResolver->ResolveMaterial(component.Material.Path) : nullptr;
			});
	}

	std::string EditorLayer::GetCurrentSceneDisplayName() const
	{
		if (CurrentScenePath.empty())
		{
			return "Untitled Scene";
		}

		return CurrentScenePath.filename().string();
	}

	void EditorLayer::ApplyPendingPickResults()
	{
		MainSceneView.ApplyPickResult(Editing);
		SecondarySceneView.ApplyPickResult(Editing);
	}
}

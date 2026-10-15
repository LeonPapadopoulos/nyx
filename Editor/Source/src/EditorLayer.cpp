#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "DefaultScene.h"
#include "EditorLinkMessages.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "NewMaterial.h"
#include "Paths.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectionSourceRegistry.h"
#include "Renderer.h"
#include "SceneSerializer.h"
#include "UIChrome.h"
#include "UIDiagnostics.h"
#include "UITheme.h"
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

	void EditorLayer::OnAttach(Nyx::Application& application)
	{
		Window = &application.GetWindow();
		Renderer = &Window->GetRenderer();
		Input = &application.GetInput();
		AssetResolver = std::make_unique<Nyx::AssetResolver>(*Renderer);
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
			.DrawStatusBar = [this](const Nyx::WindowFrameArea& area) { DrawStatusBar(area); },
		});

		Renderer->SetWorld(&Editing.Scene.GetRegistry());

		{
			std::filesystem::create_directories(Nyx::Paths::GetAssetsDir());
			std::filesystem::create_directories(Nyx::Paths::GetScenesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMeshesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMaterialsDir());
			std::filesystem::create_directories(Nyx::Paths::GetTexturesDir());

			RegisterEditorAssetTypes(AssetTypes, *this);
			AssetDb.SetAssetRoot(Nyx::Paths::GetAssetsDir());
			AssetDb.Rescan();

			AssetBrowser.SetAssetTypes(&AssetTypes);
			AssetBrowser.SetDatabase(&AssetDb);
			MainSceneView.SetAssetTypes(&AssetTypes);
			SecondarySceneView.SetAssetTypes(&AssetTypes);
			AssetBrowser.SetCurrentDirectory(std::filesystem::path("Scenes"));
		}

		RegisterDefaultPropertyWidgets();

		// Shader diagnostics open their file at the line in the source editor the preferences name
		OpenAtLine = [this](const std::filesystem::path& file, uint32_t line)
		{
			const std::string& path = *OpenedShaderFiles.insert(file.string()).first;
			PreferencesPanel.OpenSource(Nyx::SourceLocation(path.c_str(), std::max(line, 1u)), Preferences.SourceEditor);
		};
		ShaderLogWindow.SetOpenFileAtLine(OpenAtLine);
		MaterialPreview.SetOpenFileAtLine(OpenAtLine);

		// The workspace of the last session, whose layout ImGui read from imgui.ini
		Layouts.Start(FindWorkspace(Preferences.Workspace).value_or(EWorkspace::Scene));
		ApplyWorkspacePanels();
		AssetBrowser.NewMaterial = [this]()
		{
			bAskNewMaterialName = true;
			NewMaterialName = "NewMaterial";
			NewMaterialError.clear();
		};
		AssetBrowser.NewMaterialForShader = [this](const std::string& shaderPath)
		{
			CreateMaterialForShader(shaderPath);
		};
		AssetBrowser.NewMaterialInstance = [this](const std::string& materialPath)
		{
			CreateMaterialInstance(materialPath);
		};
		MaterialParameters.OpenOther = [this](const std::string& path)
		{
			OpenMaterial(path);
		};
		// Material fields pick from the assets
		Editing.DetailsContext.Assets = &AssetDb;

		// The games get what the editor compiles and the material values it edits, saved or not;
		// a game starting gets the open material's unsaved values first
		Renderer->SetShaderCompiledListener([this](const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result)
			{
				Games.SendToGames(Nyx::MakeNetMessage(Nyx::ShaderProgramMessage{ request, result }));
			});
		MaterialParameters.ValuesSent = [this](const std::string& path, const Nyx::MaterialAsset& asset)
		{
			Games.SendToGames(Nyx::MakeNetMessage(Nyx::MaterialValuesMessage{ path, asset.ToText() }));
		};
		Games.StateForNewGames = [this]()
		{
			std::vector<Nyx::Net::Message> state;
			const MaterialDocument& document = MaterialParameters.GetDocument();
			if (document.IsDirty())
			{
				state.push_back(Nyx::MakeNetMessage(Nyx::MaterialValuesMessage{ document.GetPath(), document.GetAsset().ToText() }));
			}
			return state;
		};

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
		Editing.AssetLoader.SetPostLoadContext(Nyx::ScenePostLoadContext{ .AssetResolver = AssetResolver.get() });
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
		MaterialPreview.ReleaseView();
		// imgui.ini gets the current workspace's layout too, as ImGui saves it
		Layouts.SaveCurrent();
		Renderer->SetShaderCompiledListener(nullptr);
#if NYX_DEV_TOOLS
		PainterTest.Release();
		Mockup.Release();
#endif
	}

	void EditorLayer::OnUpdate(float deltaTime)
	{
		DebugTools.BeforeFrame();

		// A workspace chosen last frame: its layout, which ImGui loads between frames
		if (Layouts.ApplyPending())
		{
			ApplyWorkspacePanels();
			Preferences.Workspace = GetWorkspaceName(Layouts.GetCurrent());
			if (!Preferences.Save(EditorPreferences::GetUserFile()))
			{
				LOG_WARNING("Couldn't save the editor preferences to '{0}'", EditorPreferences::GetUserFile().string());
			}
		}

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
		// Inside the window of the dock space, before the panels: a workspace's first layout
		Layouts.BuildDefaultLayout(ImGui::GetID("MyDockspace"));
		DrawNewMaterialPopup();

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
		{
			// What the Parameters window offers: the shaders, and the entities using the material
			MaterialParameters.ShaderChoices.clear();
			for (const AssetEntry& shader : AssetDb.GetAllByType("Shader"))
			{
				MaterialParameters.ShaderChoices.push_back(shader.RelativePath.generic_string());
			}
			MaterialParameters.MaterialChoices.clear();
			for (const AssetEntry& material : AssetDb.GetAllByType("Material"))
			{
				MaterialParameters.MaterialChoices.push_back(material.RelativePath.generic_string());
			}
			MaterialParameters.Users.clear();
			const std::string& openMaterial = MaterialParameters.GetDocument().GetPath();
			MaterialParameters.Instances = openMaterial.empty() ? std::vector<std::string>() : FindInstancesOf(openMaterial);
			Nyx::Registry& world = Editing.Scene.GetRegistry();
			world.Each<Nyx::MeshRendererComponent>([&](Nyx::Entity entity, const Nyx::MeshRendererComponent& meshRenderer)
				{
					if (openMaterial.empty() || meshRenderer.Material.Path != openMaterial)
					{
						return;
					}
					const std::string name = world.Has<Nyx::NameComponent>(entity) ? world.Get<Nyx::NameComponent>(entity).Name : std::string("Entity");
					MaterialParameters.Users.push_back({ name, [this, entity]()
						{
							Editing.Scene.GetSelection() = entity;
						} });
				});
		}
		MaterialParameters.Draw(*Renderer);
		{
			// The selected entity's mesh, for the preview's Selected Mesh
			Nyx::Mesh* selectedMesh = nullptr;
			const auto& selection = Editing.Scene.GetSelection();
			const auto& world = Editing.Scene.GetRegistry();
			if (selection && world.IsAlive(*selection) && world.Has<Nyx::MeshRendererComponent>(*selection))
			{
				selectedMesh = world.Get<Nyx::MeshRendererComponent>(*selection).MeshAsset;
			}
			// The material's shader's last compile, when it failed
			const Nyx::ShaderLogEntry* failedCompile = nullptr;
			if (MaterialParameters.GetDocument().IsOpen())
			{
				const std::string shader = MaterialParameters.GetDocument().GetAsset().GetShader();
				const Nyx::ShaderLogEntry* compile = shader.empty() ? nullptr : Renderer->GetShaderLog().FindByFile((Nyx::Paths::GetAssetsDir() / shader).make_preferred());
				failedCompile = compile && !compile->bSucceeded ? compile : nullptr;
			}
			MaterialPreview.Draw(*Renderer, MaterialParameters.GetDocument().GetPath(), selectedMesh, failedCompile);
		}
		ShaderLogWindow.Draw(Renderer->GetShaderLog());
		MainSceneView.Draw(Editing, Preferences, CameraController);
		SecondarySceneView.Draw(Editing, Preferences, CameraController);

		// Values being dragged or typed reach a running game every frame, not only when the edit ends
		Editing.DetailsContext.PreviewPendingEdits();
		Editing.Gizmo.PreviewActiveEdit(Editing.Transactions);

		// The Parameters window's undo and save keys are the material's while it has focus
		if (!SourceInspector.IsActive() && !MaterialParameters.HandleHotkeys()) Editing.HandleHotkeys();
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
#if NYX_DEV_TOOLS
		PainterTest.SetFrameRateLimits(Window->IsVSync(), Window->GetFrameRateCap());
		PainterTest.Draw(*Renderer);
		Mockup.Draw(*Renderer);
#endif
	}

	void EditorLayer::ApplyFrameRate()
	{
		// The window acts only on a change: a new swapchain for vsync, a new pace for the cap.
		Window->SetVSync(Preferences.bVSync);
		Window->SetFrameRateCap(Preferences.FrameRateCap);
	}

	void EditorLayer::DrawTitlebar(const Nyx::WindowFrameArea& area)
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
				Editing.Undo();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Redo", "Ctrl+Y")))
			{
				Editing.Redo();
			}
			Nyx::UI::Separator();
			const bool bHasSelection = Editing.Scene.GetSelection().has_value();
			if (NYX_UI(Nyx::UI::MenuItem("Copy", "Ctrl+C", false, bHasSelection)))
			{
				Editing.CopySelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Paste", "Ctrl+V", false, Editing.CanPaste())))
			{
				Editing.PasteEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Duplicate", "Ctrl+D", false, bHasSelection)))
			{
				Editing.DuplicateSelectedEntity();
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
			if (NYX_UI(Nyx::UI::MenuItem("Inspect UI source", "F8", SourceInspector.IsActive())))
			{
				SourceInspector.RequestToggle();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Preferences"))) PreferencesPanel.bOpen = true;
			// The painted widgets' colours and sizes, edited live and saved to the theme file
			NYX_UI(Nyx::UI::MenuItem("Theme", nullptr, &ThemeEditor.bOpen));
			// The material open for editing; a material opens here when double-clicked in the asset browser
			NYX_UI(Nyx::UI::MenuItem("Parameters", nullptr, &MaterialParameters.bOpen));
			// The material open, on a shape, lit or in its debug views
			NYX_UI(Nyx::UI::MenuItem("Preview", nullptr, &MaterialPreview.bOpen));
			// What the shader compiler said last, for every shader
			NYX_UI(Nyx::UI::MenuItem("Shader Log", nullptr, &ShaderLogWindow.bOpen));

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

		NextOnLine(0.0f, menuTop);
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
#if NYX_DEV_TOOLS
			NYX_UI(Nyx::UI::MenuItem("Painter Test", nullptr, &PainterTest.bOpen));
			NYX_UI(Nyx::UI::MenuItem("Mockup Overlay", nullptr, &Mockup.bOpen));
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
		const std::string sceneName = CurrentScenePath.empty() ? std::string("Untitled Scene") : CurrentScenePath.stem().string();
		const std::string sceneLabel = sceneName + "##SceneMenu";
		NextOnLine(BarGap * 0.5f - MenuButtonPadding, menuTop);
		if (NYX_UI(Nyx::UI::MenuBarButton(sceneLabel.c_str(), MenuButtonSize(sceneLabel.c_str()), ImGui::IsPopupOpen("##TitlebarSceneMenu"))))
		{
			ImGui::OpenPopup("##TitlebarSceneMenu");
		}
		ImGui::SetItemTooltip("%s", CurrentScenePath.empty() ? "Not saved yet" : CurrentScenePath.string().c_str());
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
		Play.DrawButtons(Games, Preferences, PlayButtonSize, PlayButtonSpacing);
	}

	void EditorLayer::DrawSceneMenuItems()
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
		const Nyx::LogCounts counts = Nyx::Logger::Get().GetCounts();
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
		if (!Nyx::SceneSerializer::SaveToFile(Editing.Scene.GetRegistry(), path))
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
		Nyx::ScenePostLoadContext postLoadContext{};
		postLoadContext.AssetResolver = AssetResolver.get();

		// Keep the current scene and its pending edits until the replacement is ready.
		Nyx::Registry loadedWorld;
		if (!Nyx::SceneSerializer::LoadFromFile(path, loadedWorld, postLoadContext))
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
		Editing.Scene.GetRegistry().Each<Nyx::MeshRendererComponent>(
			[this](Nyx::Entity /*entity*/, Nyx::MeshRendererComponent& component)
			{
				component.MeshAsset =
					component.Mesh.IsValid() ? AssetResolver->ResolveMesh(component.Mesh.Path) : nullptr;

				component.MaterialAsset =
					component.Material.IsValid() ? AssetResolver->ResolveMaterial(component.Material.Path) : nullptr;
			});
	}

	void EditorLayer::OpenMaterial(const std::string& path)
	{
		MaterialParameters.OpenMaterial(path);
		MaterialPreview.bOpen = true;
		Layouts.Request(EWorkspace::Materials);
	}

	void EditorLayer::OpenShader(const std::filesystem::path& file)
	{
		OpenAtLine(file, 1);
	}

	void EditorLayer::ApplyWorkspacePanels()
	{
		const bool bMaterials = Layouts.GetCurrent() == EWorkspace::Materials;
		Outliner.bOpen = !bMaterials;
		Details.bOpen = !bMaterials;
		MainSceneView.bOpen = !bMaterials;
		SecondarySceneView.bOpen = !bMaterials;
		MaterialParameters.bOpen = bMaterials;
		MaterialPreview.bOpen = bMaterials;
		ShaderLogWindow.bOpen = bMaterials;
		bAssetBrowserVisible = true;
	}

	void EditorLayer::DrawWorkspaceTabs(const Nyx::WindowFrameArea& area)
	{
		for (const EWorkspace workspace : { EWorkspace::Scene, EWorkspace::Materials })
		{
			NextOnLine(0.0f, area.Y);
			const std::string label = std::string(GetWorkspaceName(workspace)) + "##Workspace";
			if (NYX_UI(Nyx::UI::BarTab(label.c_str(), Layouts.GetCurrent() == workspace, glm::vec2(0.0f, area.Height))))
			{
				Layouts.Request(workspace);
			}
			ImGui::SetItemTooltip(workspace == EWorkspace::Scene ? "The scene: outliner, scene views, details" : "Materials: preview, parameters, Shader Log");
		}
	}

	void EditorLayer::DrawNewMaterialPopup()
	{
		if (bAskNewMaterialName)
		{
			ImGui::OpenPopup("New Material");
			bAskNewMaterialName = false;
		}
		if (!Nyx::UI::BeginPopupModal("New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		NYX_UI(ImGui::TextUnformatted("A surface shader and a material on it, named"));
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(280.0f);
		const bool bEntered = NYX_UI(Nyx::UI::InputText("##Name", &NewMaterialName, ImGuiInputTextFlags_EnterReturnsTrue));
		const std::string name = MakeMaterialName(NewMaterialName);
		if (!name.empty())
		{
			NYX_UI(ImGui::TextDisabled("Shaders/%s.slang and Materials/%s.nyxmat", name.c_str(), name.c_str()));
		}
		if (!NewMaterialError.empty())
		{
			const glm::vec4& error = Nyx::UI::GetTheme().StatusError;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(error.r, error.g, error.b, error.a));
			NYX_UI(ImGui::TextUnformatted(NewMaterialError.c_str()));
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
		if ((NYX_UI(Nyx::UI::Button("Create")) || bEntered) && CreateNewMaterial())
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	bool EditorLayer::CreateNewMaterial()
	{
		const std::optional<NewMaterialFiles> files = CreateSurfaceMaterial(Nyx::Paths::GetAssetsDir(), GetSurfaceTemplateFile(), NewMaterialName, NewMaterialError);
		if (!files)
		{
			return false;
		}
		AssetDb.Rescan();
		OpenMaterial(files->MaterialPath);
		// The shader at the line to fill in
		OpenAtLine((Nyx::Paths::GetAssetsDir() / files->ShaderPath).make_preferred(), files->ShadeLine);
		LOG_INFO("Made {} and {}", files->ShaderPath, files->MaterialPath);
		return true;
	}

	void EditorLayer::CreateMaterialInstance(const std::string& parentPath)
	{
		std::string error;
		const std::optional<NewMaterialFiles> files = Nyx::Editor::CreateMaterialInstance(Nyx::Paths::GetAssetsDir(), parentPath, error);
		if (!files)
		{
			LOG_ERROR("Couldn't make an instance of {}: {}", parentPath, error);
			return;
		}
		AssetDb.Rescan();
		InstancesFoundAt = -1.0;
		OpenMaterial(files->MaterialPath);
		LOG_INFO("Made {}", files->MaterialPath);
	}

	const std::vector<std::string>& EditorLayer::FindInstancesOf(const std::string& path)
	{
		const double now = ImGui::GetTime();
		if (path == InstancesOf && InstancesFoundAt >= 0.0 && now - InstancesFoundAt < 1.0)
		{
			return FoundInstances;
		}
		InstancesOf = path;
		InstancesFoundAt = now;
		FoundInstances.clear();
		for (const AssetEntry& material : AssetDb.GetAllByType("Material"))
		{
			Nyx::MaterialAsset asset;
			std::string error;
			if (asset.Load(material.AbsolutePath, error) && asset.GetParent() == path)
			{
				FoundInstances.push_back(material.RelativePath.generic_string());
			}
		}
		return FoundInstances;
	}

	void EditorLayer::CreateMaterialForShader(const std::string& shaderPath)
	{
		std::string error;
		const std::optional<NewMaterialFiles> files = Nyx::Editor::CreateMaterialForShader(Nyx::Paths::GetAssetsDir(), shaderPath, error);
		if (!files)
		{
			LOG_ERROR("Couldn't make a material for {}: {}", shaderPath, error);
			return;
		}
		AssetDb.Rescan();
		OpenMaterial(files->MaterialPath);
		LOG_INFO("Made {}", files->MaterialPath);
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

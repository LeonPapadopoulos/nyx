#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "DefaultScene.h"
#include "EditorLinkMessages.h"
#include "ImGuiSource.h"
#include "InputAsset.h"
#include "LiveInput.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectionSourceRegistry.h"
#include "ScatterInstances.h"
#include "EditorRenderer.h"
#include "SceneSerializer.h"
#include "UIChrome.h"

#include "imgui.h"

#include <glm/glm.hpp>

#include <algorithm>
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
			explicit RendererEditorCameras(Nyx::IEditorRenderer& renderer)
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
			Nyx::IEditorRenderer& Renderer;
		};

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
		Renderer = &application.GetEditorRenderer();
		Input = &application.GetInput();
		// The bindings as the project has them, not as a player tuned them
		InputAsset.Load(*Input, Nyx::Input::GetDefaultInputAssetPath());
		CameraController = std::make_unique<EditorCameraController>(*Input);
		SourceInspectorContext = Input->AddContext({
			.Name = "SourceInspector",
			.Priority = 950,
			.TakesDevices = Nyx::Input::DeviceBit(Nyx::Input::EDeviceKind::Keyboard) | Nyx::Input::DeviceBit(Nyx::Input::EDeviceKind::Mouse),
		});
		AssetResolver = std::make_unique<Nyx::AssetResolver>(*Renderer);
		if (!Preferences.Load(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Could not read editor preferences; using defaults.");
		}
		ApplyFrameRate();
		RegisterRuntimeReflectedSources();
		if (const auto registerModuleSources = application.GetGameModule().RegisterSources)
		{
			registerModuleSources();
		}
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
		Frame.Attach(*Window, *Renderer);

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
			AssetBrowser.SetCurrentDirectory(std::filesystem::path("Scenes"));
		}

		RegisterDefaultPropertyWidgets();

		// Shader diagnostics open their file at the line in the source editor the preferences name
		OpenAtLine = [this](const std::filesystem::path& file, uint32_t line)
		{
			const std::string& path = *OpenedShaderFiles.insert(file.string()).first;
			PreferencesPanel.OpenSource(Nyx::SourceLocation(path.c_str(), std::max(line, 1u)), Preferences.SourceEditor);
		};
		Materials.SetOpenFileAtLine(OpenAtLine);

		// The workspace of the last session, with its layout, the window where it was and the scene
		// views that were open; the workspace saved last has them as they were last
		Layouts.AddSection("Window", [this]() { return WriteWindowPlacement(Window->GetPlacement()); });
		Layouts.AddSection("SceneViews", [this]() { return SceneViews.Write(); });
		Layouts.Start(FindWorkspace(Preferences.Workspace).value_or(EWorkspace::Scene));
		if (const std::vector<std::string>* windowSection = Layouts.GetLoadedSection("Window"))
		{
			if (const std::optional<Nyx::WindowPlacement> placement = ReadWindowPlacement(*windowSection))
			{
				Window->SetPlacement(*placement);
			}
		}
		ApplyWorkspacePanels();
		AssetBrowser.NewMaterial = [this]()
		{
			Materials.AskNewMaterial();
		};
		AssetBrowser.NewMaterialForShader = [this](const std::string& shaderPath)
		{
			Materials.CreateMaterialForShader(shaderPath);
		};
		AssetBrowser.NewMaterialInstance = [this](const std::string& materialPath)
		{
			Materials.CreateMaterialInstance(materialPath);
		};
		AssetBrowser.NewPhysicsMaterial = [this]()
		{
			CreatePhysicsMaterial();
		};
		// Material fields pick from the assets
		Editing.DetailsContext.Assets = &AssetDb;

		// The games get what the editor compiles and the material values it edits, saved or not;
		// a game starting gets the open material's unsaved values first
		Renderer->SetShaderCompiledListener([this](const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result)
			{
				Games.SendToGames(Nyx::MakeNetMessage(Nyx::ShaderProgramMessage{ request, result }));
			});
		Materials.GetParameters().ValuesSent = [this](const std::string& path, const Nyx::MaterialAsset& asset)
		{
			Games.SendToGames(Nyx::MakeNetMessage(Nyx::MaterialValuesMessage{ path, asset.ToText() }));
		};
		Games.StateForNewGames = [this]()
		{
			std::vector<Nyx::Net::Message> state;
			const MaterialDocument& document = Materials.GetParameters().GetDocument();
			if (document.IsDirty())
			{
				state.push_back(Nyx::MakeNetMessage(Nyx::MaterialValuesMessage{ document.GetPath(), document.GetAsset().ToText() }));
			}
			if (PhysicsMaterial.IsDirty())
			{
				state.push_back(Nyx::MakeNetMessage(Nyx::PhysicsMaterialValuesMessage{ PhysicsMaterial.GetPath(), PhysicsMaterial.GetText() }));
			}
			// The game reads the input asset's file; unsaved edits, or A, it gets once linked
			if (InputAsset.IsLoaded() && (InputAsset.IsDirty() || InputAsset.IsShowingA()))
			{
				state.push_back(Nyx::MakeNetMessage(Nyx::MakeSetInputAssetMessage(InputAsset.GetAppliedAsset())));
			}
			return state;
		};

		SceneViews.Attach(*Renderer, &AssetTypes);
		if (const std::vector<std::string>* sceneViewSection = Layouts.GetLoadedSection("SceneViews"))
		{
			SceneViews.Read(*sceneViewSection);
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

		// Edits of the input asset share the scene's undo, and reach the input system at once
		InputAsset.Attach(Editing.Transactions);
		// So do a physics material's, and they reach the games each frame they change
		PhysicsMaterial.Attach(Editing.Transactions);
		InputWindow.Attach(InputAsset, Editing.Transactions);
		InputWindow.SetGames(&Games);
	}

	void EditorLayer::OnDetach()
	{
		// Closing the editor also ends the games it started
		Games.StopBeforeEditorCloses();

		SourceInspector.Detach();
		Frame.Detach();

		// The layout as it is, before the panels go
		Layouts.Stop();
		SceneViews.Detach();
		Materials.Release();
		Renderer->SetShaderCompiledListener(nullptr);
#if NYX_DEV_TOOLS
		PainterTest.Release();
		Mockup.Release();
#endif
	}

	void EditorLayer::OnUpdate(float deltaTime)
	{
#if NYX_DEV_TOOLS
		DebugTools.BeforeFrame();
#endif

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
		Layouts.SaveIfChanged();

		Input->GetUser().SetContextActive(SourceInspectorContext, SourceInspector.IsActive(), { .Reason = "Inspecting the UI's sources" });
		InputWindow.Record(*Input);

		RendererEditorCameras cameras(*Renderer);
		CameraController->Update(deltaTime, cameras);

		// The input asset as it is now reaches the running games, once per frame it changed, e.g.
		// while a value is dragged
		if (InputAsset.IsLoaded() && InputAsset.GetRevision() != SentInputRevision)
		{
			SentInputRevision = InputAsset.GetRevision();
			Games.SendToGames(Nyx::MakeNetMessage(Nyx::MakeSetInputAssetMessage(InputAsset.GetAppliedAsset())));
		}
		// The physics material open, as it is now, the same way, e.g. while its friction is dragged
		if (PhysicsMaterial.GetRevision() != SentPhysicsMaterialRevision)
		{
			SentPhysicsMaterialRevision = PhysicsMaterial.GetRevision();
			if (PhysicsMaterial.IsOpen())
			{
				Games.SendToGames(Nyx::MakeNetMessage(Nyx::PhysicsMaterialValuesMessage{ PhysicsMaterial.GetPath(), PhysicsMaterial.GetText() }));
			}
		}
		// The entity the Details panel's Runtime section showed last frame is asked about in the first
		// game, a few times a second
		Games.InspectEntity(Details.GetInspectedEntity());
		Games.Update();

		// A game that crashed gets a crash card, with its last log lines
		for (GameCrash& crash : Games.TakeCrashes())
		{
			std::string lastLogLines = GameLinkWindow.GetLastLogLines(crash.GameName, CrashCardsPanel::LogLineCount);
			CrashCards.Add(std::move(crash), std::move(lastLogLines));
		}

		// Scatters follow their surfaces, which their own post-load steps change, e.g. a ground edited
		// or loaded after the scatter
		Nyx::UpdateScatters(Editing.Scene.GetRegistry());

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(Editing.Scene.GetSelection());
	}

	void EditorLayer::OnUI()
	{
		// Inside the window of the dock space, before the panels: a workspace's first layout
		Layouts.BuildDefaultLayout(ImGui::GetID("MyDockspace"), SceneViews.GetTitles());
		Materials.DrawNewMaterialPopup();

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
		Details.SetGameInspection(Games.GetEntityInspection());
		Details.Draw(Editing);
		Materials.Draw(*Renderer, Editing);
		SceneViews.Draw(Editing, Preferences, *CameraController);

		// Values being dragged or typed reach a running game every frame, not only when the edit ends
		Editing.DetailsContext.PreviewPendingEdits();
		Editing.Gizmo.PreviewActiveEdit(Editing.Transactions);

		PhysicsMaterialWindow.Draw(PhysicsMaterial, Editing.Transactions);

		// The Parameters window's undo and save keys are the material's while it has focus; the
		// Physics Material window's save key is its material's
		if (!SourceInspector.IsActive() && !Materials.HandleHotkeys() && !PhysicsMaterialWindow.HandleHotkeys(PhysicsMaterial)) Editing.HandleHotkeys();
		PreferencesPanel.Draw(Preferences, SourceInspector);
		// The theme window remembers the theme loaded in the preferences
		const std::string theme = Preferences.Theme;
		ThemeEditor.Draw(Preferences);
		if (Preferences.Theme != theme && !Preferences.Save(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Couldn't save the editor preferences to '{0}'", EditorPreferences::GetUserFile().string());
		}
		ApplyFrameRate();

		InputWindow.Draw(*Input);

		if (bShowGameLink)
		{
			GameLinkWindow.Draw(bShowGameLink);
		}

		Play.DrawSetupsWindow(Preferences);

		if (const std::optional<GameCrash> restart = CrashCards.Draw())
		{
			Games.Restart(*restart);
		}

#if NYX_DEV_TOOLS
		DebugTools.DrawWindows();
		PainterTest.SetFrameRateLimits(Renderer->IsVSync(), Window->GetFrameRateCap());
		PainterTest.Draw(*Renderer);
		Mockup.Draw(*Renderer);
#endif
	}

	void EditorLayer::ApplyFrameRate()
	{
		// Each acts only on a change: the renderer makes a new swapchain for vsync and the views'
		// targets for the samples, the window a new pace for the cap.
		Renderer->SetVSync(Preferences.bVSync);
		Renderer->SetMsaaSamples(Preferences.MsaaSamples);
		Window->SetFrameRateCap(Preferences.FrameRateCap);
	}

	bool EditorLayer::SaveCurrentScene(const std::filesystem::path& path)
	{
		if (!Nyx::SceneSerializer::SaveToFile(Editing.Scene.GetRegistry(), path))
		{
			LOG_ERROR("Failed to save scene to '{0}'", path.string());
			Frame.SetSceneFileStatus("Couldn't save " + path.filename().string(), true);
			return false;
		}

		LOG_INFO("Saved scene to '{0}'", path.string());
		Frame.SetSceneFileStatus("Saved " + path.filename().string() + " at " + TimeOfDay(), false);
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
			Frame.SetSceneFileStatus("Couldn't open " + path.filename().string(), true);
			return false;
		}

		ForgetPreviousScene();
		Editing.Scene.GetRegistry() = std::move(loadedWorld);
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath = path;

		LOG_INFO("Loaded scene from '{0}'", path.string());
		Frame.SetSceneFileStatus("Opened " + path.filename().string() + " at " + TimeOfDay(), false);
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
		Frame.SetSceneFileStatus("New scene, not saved yet", false);
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
		Materials.OpenMaterial(path);
	}

	void EditorLayer::OpenShader(const std::filesystem::path& file)
	{
		OpenAtLine(file, 1);
	}

	void EditorLayer::OpenPhysicsMaterial(const std::string& path)
	{
		std::string error;
		if (!PhysicsMaterial.Open(Nyx::Paths::GetAssetSearchDirs(), path, error))
		{
			LOG_ERROR("Couldn't open physics material '{0}': {1}", path, error);
		}
	}

	void EditorLayer::CreatePhysicsMaterial()
	{
		// In the folder shown, named apart from what is there
		const std::filesystem::path folder = AssetBrowser.GetCurrentDirectory();
		std::filesystem::path relative;
		for (int number = 1; relative.empty() || std::filesystem::exists(Nyx::Paths::GetAssetsDir() / relative); ++number)
		{
			const std::string name = number == 1 ? "NewPhysicsMaterial" : "NewPhysicsMaterial" + std::to_string(number);
			relative = folder / (name + std::string(Nyx::PhysicsMaterialAsset::Extension));
		}

		// The engine's values, written out to be changed
		Nyx::PhysicsMaterialAsset asset;
		std::string error;
		asset.Parse("# How a collider's surface slides and bounces, and how heavy its stuff is. A game's own\n"
					"# numbers are Extra.<Name> = value, e.g. Extra.Flammability = 0.8\n"
					"Friction = 0.5\n"
					"Restitution = 0\n"
					"Density = 1000\n",
			error);
		if (!asset.Save(Nyx::Paths::GetAssetsDir() / relative, error))
		{
			LOG_ERROR("Couldn't make a physics material: {0}", error);
			return;
		}
		AssetDb.Rescan();
		OpenPhysicsMaterial(relative.generic_string());
	}

	void EditorLayer::ApplyWorkspacePanels()
	{
		const bool bMaterials = Layouts.GetCurrent() == EWorkspace::Materials;
		Outliner.bOpen = !bMaterials;
		Details.bOpen = !bMaterials;
		SceneViews.bShown = !bMaterials;
		Materials.GetParameters().bOpen = bMaterials;
		Materials.GetPreview().bOpen = bMaterials;
		Materials.GetShaderLog().bOpen = bMaterials;
		bAssetBrowserVisible = true;
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
		SceneViews.ApplyPickResults(Editing);
	}
}

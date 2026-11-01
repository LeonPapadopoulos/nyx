#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "DefaultScene.h"
#include "Prefab.h"
#include "PrefabEdits.h"
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
#include "JobSystem.h"
#include "TerrainChunks.h"
#include "SceneOutlineEdits.h"
#include "TimeOfDay.h"
#include "TransformComponent.h"
#include "EditorRenderer.h"
#include "SceneSerializer.h"
#include "UIChrome.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <ctime>
#include <utility>
#include <vector>

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
		Frame.Attach(*Window, *Renderer, application.GetFrameStats());

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
			for (const Nyx::AssetValuesMessage& values : AssetEditors.GetUnsavedValues())
			{
				state.push_back(Nyx::MakeNetMessage(values));
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

		// Which of the outliner's folders are closed, per scene
		Outliner.Folders.SetFile(Nyx::Paths::GetSavedDir() / "OutlinerFolders.txt");

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

		// Edits, undo and redo load the assets they need, as loading a scene does, and make what takes
		// long in the background
		Editing.AssetLoader.SetPostLoadContext(Nyx::ScenePostLoadContext{ .AssetResolver = AssetResolver.get(), .Background = &Nyx::JobSystem::Get() });
		Editing.Assets = AssetResolver.get();
		Editing.AssetTypes = &AssetTypes;
		Editing.AssetFileWritten = [this]()
		{
			AssetDb.Rescan();
		};
		Editing.OpenPrefab = [this](const std::string& prefabPath)
		{
			OpenPrefab(prefabPath);
		};
		Editing.Transactions.Subscribe(&Editing.AssetLoader);

		// Edits show up live in the game started with Play
		Editing.Transactions.Subscribe(&Games.GetEditSubscriber());

		// Then prefabs changed by an edit are saved and their instances made again, as when another
		// program changes their files
		Editing.Transactions.Subscribe(&Editing.Prefabs);
		for (const std::filesystem::path& folder : Nyx::Paths::GetAssetSearchDirs())
		{
			PrefabFiles.Watch(folder);
		}

		// Then children are placed relative to their parents, and the games get where they moved
		Editing.Hierarchy.SetOnMoved([this](const std::vector<Nyx::Entity>& moved)
			{
				Games.GetEditSubscriber().SendTransforms(moved);
			});
		Editing.Transactions.Subscribe(&Editing.Hierarchy);

		// Edits of the input asset share the scene's undo, and reach the input system at once
		InputAsset.Attach(Editing.Transactions);
		// So do the assets edited by reflection, which reach the games each frame they change, and are
		// read again when their files change while they have no changes
		AssetEditors.SetAssetFolders(Nyx::Paths::GetAssetSearchDirs());
		AssetEditors.Attach(Editing.Transactions);
		AssetEditors.Watch();
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

		// Prefab mode left, and what that was for, and a prefab opened, as asked last frame
		if (std::exchange(bLeavePrefab, false))
		{
			ClosePrefab();
			if (const std::function<void()> then = std::exchange(AfterLeavingPrefab, {}))
			{
				then();
			}
		}
		if (PendingPrefab)
		{
			EnterPrefab(*std::exchange(PendingPrefab, std::nullopt));
		}

		// Prefabs whose files another program changed: their instances follow them
		for (const std::filesystem::path& changed : PrefabFiles.TakeChanges())
		{
			if (changed.extension() == Nyx::PrefabExtension)
			{
				if (const std::optional<std::string> prefabPath = GetAssetPathOf(changed))
				{
					Editing.Prefabs.FileChanged(*prefabPath);
				}
			}
		}

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
		// The assets open, as they are now, the same way, e.g. while a physics material's friction is
		// dragged
		AssetEditors.Update();
		for (const Nyx::AssetValuesMessage& values : AssetEditors.TakeValuesToSend())
		{
			Games.SendToGames(Nyx::MakeNetMessage(values));
		}
		// The entity the Details panel's Runtime section showed last frame is asked about in the first
		// game, a few times a second; none of a prefab's
		Games.InspectEntity(Prefab.IsOpen() ? std::nullopt : Details.GetInspectedEntity());
		Games.Update();

		// A game that crashed gets a crash card, with its last log lines
		for (GameCrash& crash : Games.TakeCrashes())
		{
			std::string lastLogLines = GameLinkWindow.GetLastLogLines(crash.GameName, CrashCardsPanel::LogLineCount);
			CrashCards.Add(std::move(crash), std::move(lastLogLines));
		}

		// Scatters follow their surfaces, which their own post-load steps change, e.g. a ground edited
		// or loaded after the scatter; made in the background, so the editor never waits for one
		Nyx::Registry& shown = GetEditing().Scene.GetRegistry();
		Nyx::UpdateScatters(shown, &Nyx::JobSystem::Get());
		// SDF terrains' chunks round the scene views' cameras, as they were last drawn
		{
			std::vector<glm::vec3> viewpoints;
			for (const uint64_t view : Renderer->GetSceneViewIds())
			{
				Nyx::SceneViewCameraData camera;
				if (Renderer->GetSceneViewCameraData(view, camera))
				{
					viewpoints.push_back(camera.CameraWorldPos);
				}
			}
			Nyx::UpdateTerrainChunks(shown, viewpoints, Nyx::JobSystem::Get());
		}
		// Mesh renderers made by code, e.g. a game module's post-load step, get their assets
		Nyx::ResolveChangedMeshRenderers(shown, *AssetResolver);
		// The sun turns to the time of day as it is set, edited or undone; time doesn't run here
		Nyx::ApplyTimeOfDay(shown);

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(GetEditing().Scene.GetSelection());
	}

	void EditorLayer::OnUI()
	{
		// Inside the window of the dock space, before the panels: a workspace's first layout
		Layouts.BuildDefaultLayout(ImGui::GetID("MyDockspace"), SceneViews.GetTitles());
		Materials.DrawNewMaterialPopup();

		SceneEditor& editing = GetEditing();
		if (!SourceInspector.IsActive()) editing.Gizmo.TickHotkeys();
		// F alone: Ctrl+F is the outliner's search
		if (!SourceInspector.IsActive() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiKey_F)) FrameSelection();

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

		Outliner.Draw(editing);
		Details.SetGameInspection(Games.GetEntityInspection());
		Details.Draw(editing);
		Materials.Draw(*Renderer, editing);
		SceneViews.bGameInFront = Games.IsRunning() && !Window->IsFocused();
		SceneViews.Draw(editing, Preferences, *CameraController);

		// Values being dragged or typed reach a running game every frame, not only when the edit ends
		editing.DetailsContext.PreviewPendingEdits();
		editing.Gizmo.PreviewActiveEdit(editing.Transactions);

		AssetEditorWindows.Draw(AssetEditors, Editing.Transactions, &AssetDb);

		// The Parameters window's undo and save keys are the material's while it has focus; an asset
		// window's save key is its asset's
		if (!SourceInspector.IsActive() && !Materials.HandleHotkeys() && !AssetEditorWindows.HandleHotkeys(AssetEditors))
		{
			// In prefab mode, Ctrl+S saves the prefab
			if (Prefab.IsOpen() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
			{
				SavePrefab();
			}
			editing.HandleHotkeys();
		}
		DrawLeavePrefabPopup();
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
		PlanMap.Draw(*Renderer, editing);
		PlanSeeds.Draw(*Renderer, editing);

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
		SkyTables.Draw(*Renderer);
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
		const Nyx::SceneOutline outline = MakeSavedOutline(Editing.Scene);
		// Instances of prefabs as what differs from their prefabs
		if (!Nyx::SceneSerializer::SaveToFile(Editing.Scene.GetRegistry(), path, &outline, AssetResolver.get()))
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
		// Out of prefab mode first
		if (Prefab.IsOpen())
		{
			LeavePrefab([this, path]() { LoadCurrentScene(path); });
			return false;
		}

		Nyx::ScenePostLoadContext postLoadContext{};
		postLoadContext.AssetResolver = AssetResolver.get();
		postLoadContext.Background = &Nyx::JobSystem::Get();

		// Keep the current scene and its pending edits until the replacement is ready.
		Nyx::Registry loadedWorld;
		Nyx::SceneOutline loadedOutline;
		if (!Nyx::SceneSerializer::LoadFromFile(path, loadedWorld, postLoadContext, &loadedOutline))
		{
			LOG_ERROR("Failed to load scene from '{0}'", path.string());
			Frame.SetSceneFileStatus("Couldn't open " + path.filename().string(), true);
			return false;
		}

		ForgetPreviousScene();
		Editing.Scene.GetRegistry() = std::move(loadedWorld);
		Editing.Scene.GetOutline() = std::move(loadedOutline);
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		// A child's local values are what counts in the editor; its saved world transform may be older
		Editing.Hierarchy.Update();
		CurrentScenePath = path;
		Outliner.Folders.SetScene(path);

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
			Outliner.Folders.MoveScene(path);
			AssetDb.Rescan();
			return true;
		}

		return false;
	}

	bool EditorLayer::NewScene()
	{
		if (Prefab.IsOpen())
		{
			LeavePrefab([this]() { NewScene(); });
			return false;
		}

		ForgetPreviousScene();
		Editing.Scene.GetRegistry().Clear();
		Editing.Scene.GetOutline() = {};
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath.clear();
		Outliner.Folders.SetScene({});
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

	void EditorLayer::OpenReflectedAsset(const std::string& path)
	{
		std::string error;
		if (!AssetEditors.Open(path, error))
		{
			LOG_ERROR("Couldn't open '{0}': {1}", path, error);
		}
	}

	void EditorLayer::CreateReflectedAsset(const Nyx::ReflectedAssetType& type, const std::filesystem::path& folder)
	{
		std::string error;
		const std::string path = AssetEditors.Create(type, folder, error);
		if (path.empty())
		{
			LOG_ERROR("Couldn't make a {0}: {1}", type.DisplayName, error);
			return;
		}
		AssetDb.Rescan();
		OpenReflectedAsset(path);
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
		SceneViews.ApplyPickResults(GetEditing());
	}

	void EditorLayer::FrameSelection()
	{
		const std::optional<Nyx::Entity> selection = GetEditing().Scene.GetSelection();
		const Nyx::Registry& registry = GetEditing().Scene.GetRegistry();
		if (!selection || !registry.IsAlive(*selection))
		{
			return;
		}

		// What it draws, else a box of its scale where it is
		if (const std::optional<Nyx::BoundingBox> bounds = Renderer->GetEntityDrawnBounds(registry, *selection))
		{
			CameraController->Frame(bounds->Min, bounds->Max);
		}
		else if (registry.Has<Nyx::TransformComponent>(*selection))
		{
			const Nyx::TransformComponent& transform = registry.Get<Nyx::TransformComponent>(*selection);
			const glm::vec3 halfSize = glm::abs(transform.Scale) * 0.5f;
			CameraController->Frame(transform.Position - halfSize, transform.Position + halfSize);
		}
	}

	void EditorLayer::OpenPrefab(const std::string& prefabPath)
	{
		if (Prefab.IsOpen() && Prefab.GetPath() == prefabPath)
		{
			return;
		}
		LeavePrefab([this, prefabPath]() { PendingPrefab = prefabPath; });
	}

	bool EditorLayer::SavePrefab()
	{
		const std::string name = std::filesystem::path(Prefab.GetPath()).filename().string();
		std::string error;
		if (!Prefab.Save(error))
		{
			LOG_ERROR("Couldn't save prefab '{0}': {1}", Prefab.GetPath(), error);
			Frame.SetSceneFileStatus("Couldn't save " + name + ": " + error, true);
			return false;
		}
		Frame.SetSceneFileStatus("Saved " + name + " at " + TimeOfDay(), false);
		return true;
	}

	void EditorLayer::LeavePrefab(std::function<void()> then)
	{
		AfterLeavingPrefab = std::move(then);
		if (Prefab.IsDirty())
		{
			bAskToLeavePrefab = true;
		}
		else
		{
			bLeavePrefab = true;
		}
	}

	void EditorLayer::EnterPrefab(const std::string& prefabPath)
	{
		Editing.CancelPendingEdits();
		std::string error;
		if (!Prefab.Open(prefabPath, error))
		{
			LOG_ERROR("Couldn't open prefab '{0}': it {1}", prefabPath, error);
			Frame.SetSceneFileStatus("Couldn't open " + std::filesystem::path(prefabPath).filename().string(), true);
			return;
		}
		SceneEditor& prefab = Prefab.GetEditing();
		// Widgets keep no drag or text edit of the scene's entities for the prefab's, nor back
		prefab.SceneRevision = ++Editing.SceneRevision;
		++Editing.SceneRevision;
		Renderer->SetWorld(&prefab.Scene.GetRegistry());

		// The views look at the whole prefab the way they looked, and where they looked in the scene
		// again on the way back
		const Nyx::Registry& world = prefab.Scene.GetRegistry();
		std::optional<Nyx::BoundingBox> box;
		world.Each<Nyx::TransformComponent>(
			[&](Nyx::Entity entity, const Nyx::TransformComponent& transform)
			{
				const glm::vec3 half = glm::abs(transform.Scale) * 0.5f;
				const Nyx::BoundingBox bounds = Renderer->GetEntityDrawnBounds(world, entity).value_or(Nyx::BoundingBox{ transform.Position - half, transform.Position + half });
				box = box ? Nyx::BoundingBox{ glm::min(box->Min, bounds.Min), glm::max(box->Max, bounds.Max) } : bounds;
			});
		SceneCameras.clear();
		for (const std::unique_ptr<SceneViewPanel>& view : SceneViews.GetViews())
		{
			SceneViewCamera camera{ .View = view->GetViewId() };
			if (!Renderer->GetSceneViewEditorCameraTransform(camera.View, camera.Position, camera.Rotation))
			{
				continue;
			}
			SceneCameras.push_back(camera);
			if (box)
			{
				const glm::vec3 position = CameraController->GetFramingPosition(box->Min, box->Max, EditorCameraController::GetForward(camera.Rotation));
				Renderer->SetSceneViewEditorCameraTransform(camera.View, position, camera.Rotation);
			}
		}
		Frame.SetSceneFileStatus("Editing prefab " + std::filesystem::path(prefabPath).filename().string(), false);
	}

	void EditorLayer::ClosePrefab()
	{
		if (!Prefab.IsOpen())
		{
			return;
		}
		Prefab.GetEditing().CancelPendingEdits();
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		for (const SceneViewCamera& camera : SceneCameras)
		{
			Renderer->SetSceneViewEditorCameraTransform(camera.View, camera.Position, camera.Rotation);
		}
		SceneCameras.clear();
		Prefab.Close();
		Frame.SetSceneFileStatus("Back in " + GetCurrentSceneDisplayName(), false);
	}

	void EditorLayer::DrawLeavePrefabPopup()
	{
		constexpr const char* Title = "Save Prefab Changes?";
		if (bAskToLeavePrefab)
		{
			ImGui::OpenPopup(Title);
			bAskToLeavePrefab = false;
		}
		if (!Nyx::UI::BeginPopupModal(Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		NYX_UI(ImGui::Text("%s has changes that aren't saved.", std::filesystem::path(Prefab.GetPath()).filename().string().c_str()));
		ImGui::Spacing();
		// Enter saves, Escape cancels; a save that fails leaves the prefab open, and the status bar
		// says why
		if (NYX_UI(Nyx::UI::Button("Save")) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
		{
			bLeavePrefab = SavePrefab();
			// Not leaving: what it was for isn't done either
			if (!bLeavePrefab)
			{
				AfterLeavingPrefab = {};
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Discard")))
		{
			bLeavePrefab = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			AfterLeavingPrefab = {};
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

#pragma once

#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "AssetResolver.h"
#include "FileWatcher.h"
#include "AssetTypeRegistry.h"
#include "CrashCardsPanel.h"
#include "DetailsPanel.h"
#include "EditorCameraController.h"
#include "EditorFrame.h"
#include "EditorPreferences.h"
#include "GameLinkPanel.h"
#include "InputAssetDocument.h"
#include "InputPanel.h"
#include "Layer.h"
#include "MaterialWorkspace.h"
#include "Paths.h"
#include "PlayControls.h"
#include "PlaySession.h"
#include "PreferencesWindow.h"
#include "PrefabMode.h"
#include "ReflectedAssetEditors.h"
#include "ReflectedAssetWindows.h"
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneOutlinerPanel.h"
#include "SceneViewSet.h"
#include "ThemeEditorWindow.h"
#include "Workspaces.h"
#include "UISourceInspector.h"
#include "Window.h"

#if NYX_DEV_TOOLS
#include "ImGuiDebugTools.h"
#include "MockupOverlay.h"
#include "PainterTestWindow.h"
#include "SkyTablesWindow.h"
#endif
#include "TerrainPlanWindow.h"
#include "TerrainSeedsWindow.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx::Editor
{
	// The whole editor as one layer of the application. It owns the scene being edited
	// (SceneEditor), the panels that show it, the Materials workspace and Play (PlaySession), and
	// wires them together; EditorFrame draws the window's titlebar and status bar from them. It
	// opens, saves and replaces scenes.
	class EditorLayer : public Nyx::ILayer
	{
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnUI() override;

	public:
		bool SaveCurrentScene(const std::filesystem::path& path);
		bool LoadCurrentScene(const std::filesystem::path& path);

		bool SaveScene();
		bool SaveSceneAs(const std::filesystem::path& path);
		bool NewScene();

		void ResolveSceneRuntimeAssets();

		std::string GetCurrentSceneDisplayName() const;

		// Opens a material ("Materials/Stone.nyxmat") in the Materials workspace
		void OpenMaterial(const std::string& path);
		// Opens a shader file in the source editor of the preferences
		void OpenShader(const std::filesystem::path& file);
		// Opens an asset of a reflected asset type, e.g. "PhysicsMaterials/Wood.nyxphysmat", in its window
		void OpenReflectedAsset(const std::string& path);
		// The asset browser's New <DisplayName>: a file of the type's defaults in `folder` (relative to
		// the assets folder), opened
		void CreateReflectedAsset(const Nyx::ReflectedAssetType& type, const std::filesystem::path& folder);

		// Opens a prefab ("Prefabs/Lamp.nyxprefab") in prefab mode from the next frame on, leaving the
		// one open first
		void OpenPrefab(const std::string& prefabPath);
		// Writes the prefab open in prefab mode; the scene's instances follow it
		bool SavePrefab();
		// Back to the scene, then `then`, from the next frame on; asks first whether to save the
		// prefab's changes, if it has any, and does neither if that is cancelled
		void LeavePrefab(std::function<void()> then = {});

	private:
		// Vsync and the frame rate cap, from the preferences, to the window
		void ApplyFrameRate();

		void ApplyPendingPickResults();

		// Flies a scene view's camera to see the selected entity whole (F)
		void FrameSelection();

		// Opens the workspace's panels and closes the other's, e.g. after switching
		void ApplyWorkspacePanels();

		// Called before destroying the old registry, once its replacement is ready.
		void ForgetPreviousScene();

		// What the panels and scene views show and edit: the prefab's in prefab mode, else the scene's
		SceneEditor& GetEditing()
		{
			return Prefab.IsOpen() ? Prefab.GetEditing() : Editing;
		}

		// Prefab mode begun, with the scene views looking at the prefab, and ended, with them looking
		// where they looked in the scene
		void EnterPrefab(const std::string& prefabPath);
		void ClosePrefab();
		// Whether to save the prefab's changes before leaving it: Save, Discard or Cancel
		void DrawLeavePrefabPopup();

	private:
		Nyx::IWindow* Window = nullptr;
		Nyx::IEditorRenderer* Renderer = nullptr;
		Nyx::Input::InputSystem* Input = nullptr;

		// Turns the asset paths in components into the renderer's assets; created in OnAttach.
		std::unique_ptr<Nyx::AssetResolver> AssetResolver;

		// The asset folders, for prefab files changed by other programs (PrefabSubscriber::FileChanged())
		Nyx::FileWatcher PrefabFiles;

		// The asset types the editor knows (RegisterEditorAssetTypes()), filled in OnAttach
		AssetTypeRegistry AssetTypes;
		AssetDatabase AssetDb{ AssetTypes };
		std::filesystem::path CurrentScenePath;

		SceneEditor Editing;

		// A prefab open on its own, which the panels show in place of the scene. Opening and leaving
		// one wait for the next frame, as the panels hold its editing while they draw: the prefab to
		// open, whether to leave, what to do once left, and whether to ask about its changes first.
		PrefabMode Prefab{ Editing };
		std::optional<std::string> PendingPrefab;
		bool bLeavePrefab = false;
		std::function<void()> AfterLeavingPrefab;
		bool bAskToLeavePrefab = false;
		// The scene views' cameras as they were in the scene
		struct SceneViewCamera
		{
			uint64_t View = 0;
			glm::vec3 Position{ 0.0f };
			glm::vec3 Rotation{ 0.0f };
		};
		std::vector<SceneViewCamera> SceneCameras;

		EditorPreferences Preferences;
#if NYX_DEV_TOOLS
		ImGuiDebugTools DebugTools;
		PainterTestWindow PainterTest;
		MockupOverlay Mockup;
		SkyTablesWindow SkyTables;
#endif
		UISourceInspector SourceInspector;

		// Panels and windows
		AssetBrowserPanel AssetBrowser;
		bool bAssetBrowserVisible = true;
		SceneOutlinerPanel Outliner;
		DetailsPanel Details;
		// Paths of files opened from shader diagnostics: the source editor opens them on a worker,
		// which reads the path after the diagnostic may be gone
		std::set<std::string> OpenedShaderFiles;
		OpenFileAtLine OpenAtLine;

		// The Scene and Materials workspaces' layouts
		Workspaces Layouts{ Nyx::Paths::GetSavedDir() / "Layouts" };

		// The Parameters, Preview and Shader Log windows, and New Material
		MaterialWorkspace Materials{ AssetDb, Layouts };

		// The scene views, as many as are open (Window > New Scene View)
		SceneViewSet SceneViews;
		// Flies the scene views' free cameras; made in OnAttach, with the application's input
		std::unique_ptr<EditorCameraController> CameraController;

		// On while the UI source inspector is: it takes every key and click
		Nyx::Input::ContextHandle SourceInspectorContext;
		SceneFilePopups SceneFiles;
		PreferencesWindow PreferencesPanel;
		ThemeEditorWindow ThemeEditor;
		PlayControls Play;
		CrashCardsPanel CrashCards;

		// The input asset, Input/Default.nyxinput: the scene views' bindings, edited in
		// Window > Input's Tuning tab with the editor's undo
		InputAssetDocument InputAsset;
		// The asset's revision the running games last got
		uint64_t SentInputRevision = 0;

		// Window > Input: the editor's own input, with its traces
		InputPanel InputWindow;
		// The world plan of the scene's SDF terrain from above
		TerrainPlanWindow PlanMap;
		// Many seeds of it side by side
		TerrainSeedsWindow PlanSeeds;

		// The assets of reflected asset types being edited (physics materials, a game's own types),
		// with the scene's undo, and their windows
		ReflectedAssetEditors AssetEditors;
		ReflectedAssetWindows AssetEditorWindows;

		// The games' logs and every message of their editor links
		GameLinkPanel GameLinkWindow;
		bool bShowGameLink = true;

		// The games started with Play; edits reach them from Play on
		PlaySession Games{ Editing.Scene, GameLinkWindow };

		// The window's titlebar and status bar, and the menus in them
		EditorFrame Frame{ EditorFrame::Parts{
			.Editing = Editing,
			.CurrentScenePath = CurrentScenePath,
			.NewScene = [this]() { NewScene(); },
			.SaveScene = [this]() { SaveScene(); },
			.SceneFiles = SceneFiles,
			.Prefab = Prefab,
			.SavePrefab = [this]() { SavePrefab(); },
			.LeavePrefab = [this]() { LeavePrefab(); },
			.SourceInspector = SourceInspector,
			.PreferencesPanel = PreferencesPanel,
			.ThemeEditor = ThemeEditor,
			.Materials = Materials,
			.bAssetBrowserVisible = bAssetBrowserVisible,
			.InputWindow = InputWindow,
			.PlanMap = PlanMap,
			.PlanSeeds = PlanSeeds,
			.bShowGameLink = bShowGameLink,
			.SceneViews = SceneViews,
#if NYX_DEV_TOOLS
			.DebugTools = DebugTools,
			.PainterTest = PainterTest,
			.Mockup = Mockup,
			.SkyTables = SkyTables,
#endif
			.Layouts = Layouts,
			.ResetLayout = [this]()
			{
				Layouts.RequestDefaultLayout();
				ApplyWorkspacePanels();
			},
			.Play = Play,
			.Games = Games,
			.Preferences = Preferences,
		} };
	};

	// Every asset type the editor knows (EditorAssetTypes.cpp). Opening one calls into `editor`,
	// which must outlive the registry's use.
	void RegisterEditorAssetTypes(AssetTypeRegistry& registry, EditorLayer& editor);
}

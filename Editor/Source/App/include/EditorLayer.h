#pragma once

#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "AssetResolver.h"
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
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneOutlinerPanel.h"
#include "SceneViewPanel.h"
#include "ThemeEditorWindow.h"
#include "Workspaces.h"
#include "UISourceInspector.h"
#include "Window.h"

#if NYX_DEV_TOOLS
#include "ImGuiDebugTools.h"
#include "MockupOverlay.h"
#include "PainterTestWindow.h"
#endif

#include <filesystem>
#include <memory>
#include <set>
#include <string>

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

	private:
		// Vsync and the frame rate cap, from the preferences, to the window
		void ApplyFrameRate();

		void ApplyPendingPickResults();

		// Opens the workspace's panels and closes the other's, e.g. after switching
		void ApplyWorkspacePanels();

		// Called before destroying the old registry, once its replacement is ready.
		void ForgetPreviousScene();

	private:
		Nyx::IWindow* Window = nullptr;
		Nyx::IEditorRenderer* Renderer = nullptr;
		Nyx::Input::InputSystem* Input = nullptr;

		// Turns the asset paths in components into the renderer's assets; created in OnAttach.
		std::unique_ptr<Nyx::AssetResolver> AssetResolver;

		// The asset types the editor knows (RegisterEditorAssetTypes()), filled in OnAttach
		AssetTypeRegistry AssetTypes;
		AssetDatabase AssetDb{ AssetTypes };
		std::filesystem::path CurrentScenePath;

		SceneEditor Editing;

		EditorPreferences Preferences;
#if NYX_DEV_TOOLS
		ImGuiDebugTools DebugTools;
		PainterTestWindow PainterTest;
		MockupOverlay Mockup;
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

		SceneViewPanel MainSceneView{ "Scene" };
		SceneViewPanel SecondarySceneView{ "Scene 2" };
		// Flies the scene views' free cameras; made in OnAttach, with the application's input
		std::unique_ptr<EditorCameraController> CameraController;

		// On while the UI source inspector is: it takes every key and click
		Nyx::Input::ContextHandle SourceInspectorContext;
		bool bSecondaryViewShowsGameView = false;
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
			.SourceInspector = SourceInspector,
			.PreferencesPanel = PreferencesPanel,
			.ThemeEditor = ThemeEditor,
			.Materials = Materials,
			.bAssetBrowserVisible = bAssetBrowserVisible,
			.InputWindow = InputWindow,
			.bShowGameLink = bShowGameLink,
			.SecondarySceneView = SecondarySceneView,
			.bSecondaryViewShowsGameView = bSecondaryViewShowsGameView,
#if NYX_DEV_TOOLS
			.DebugTools = DebugTools,
			.PainterTest = PainterTest,
			.Mockup = Mockup,
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

#pragma once

#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "AssetResolver.h"
#include "AssetTypeRegistry.h"
#include "CrashCardsPanel.h"
#include "DetailsPanel.h"
#include "EditorCameraController.h"
#include "EditorPreferences.h"
#include "GameLinkPanel.h"
#include "ImGuiDebugTools.h"
#include "Layer.h"
#include "MaterialParametersPanel.h"
#include "MaterialPreviewPanel.h"
#include "ShaderLogPanel.h"
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
	// (SceneEditor), the panels that show it, and Play (PlaySession); it draws the window's
	// titlebar and status bar, and opens, saves and replaces scenes.
	class EditorLayer : public Nyx::Engine::ILayer
	{
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		void OnAttach(Nyx::Engine::Application& application) override;
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
		// The window's frame (IWindow::SetFrameContent()). The titlebar, one row: the logo, the
		// menus, the open scene and Play. The status bar: the last scene saved or opened, errors
		// logged, the graphics API and the frame rate.
		void DrawTitlebar(const Nyx::WindowFrameArea& area);
		void DrawStatusBar(const Nyx::WindowFrameArea& area);

		// New, Save, Save As and Load, in the File menu and the scene's own
		void DrawSceneMenuItems();

		// What the status bar says about the last scene saved, opened or made
		void SetSceneFileStatus(std::string text, bool bFailed);

		// Vsync and the frame rate cap, from the preferences, to the window
		void ApplyFrameRate();

		void ApplyPendingPickResults();

		// Opens the workspace's panels and closes the other's, e.g. after switching
		void ApplyWorkspacePanels();
		// The titlebar's workspace tabs
		void DrawWorkspaceTabs(const Nyx::WindowFrameArea& area);
		// "New Material": asks for a name, then makes a shader and a material and opens them
		void DrawNewMaterialPopup();
		bool CreateNewMaterial();
		void CreateMaterialForShader(const std::string& shaderPath);
		void CreateMaterialInstance(const std::string& parentPath);
		// The materials whose parent is `path`, read from their files at most once a second
		const std::vector<std::string>& FindInstancesOf(const std::string& path);

		// Called before destroying the old registry, once its replacement is ready.
		void ForgetPreviousScene();

	private:
		Nyx::IWindow* Window = nullptr;
		Nyx::IRenderer* Renderer = nullptr;
		Nyx::Input::InputSystem* Input = nullptr;

		// Turns the asset paths in components into the renderer's assets; created in OnAttach.
		std::unique_ptr<Nyx::Engine::AssetResolver> AssetResolver;

		// The asset types the editor knows (RegisterEditorAssetTypes()), filled in OnAttach
		AssetTypeRegistry AssetTypes;
		AssetDatabase AssetDb{ AssetTypes };
		std::filesystem::path CurrentScenePath;

		SceneEditor Editing;

		// The last scene saved, opened or made, for the status bar, and whether that failed
		std::string SceneFileStatus;
		bool bSceneFileStatusFailed = false;

		EditorPreferences Preferences;
		ImGuiDebugTools DebugTools;
#if NYX_DEV_TOOLS
		PainterTestWindow PainterTest;
		MockupOverlay Mockup;
#endif
		UISourceInspector SourceInspector;

		// Panels and windows
		AssetBrowserPanel AssetBrowser;
		bool bAssetBrowserVisible = true;
		SceneOutlinerPanel Outliner;
		DetailsPanel Details;
		// The material open for editing
		MaterialParametersPanel MaterialParameters{ Nyx::Paths::GetAssetsDir() };
		// The material open, on a shape in a world of its own
		MaterialPreviewPanel MaterialPreview;
		// Every shader's last compile, and what the compiler said
		ShaderLogPanel ShaderLogWindow;
		// Paths of files opened from shader diagnostics: the source editor opens them on a worker,
		// which reads the path after the diagnostic may be gone
		std::set<std::string> OpenedShaderFiles;
		OpenFileAtLine OpenAtLine;

		// The Scene and Materials workspaces' layouts
		Workspaces Layouts{ Nyx::Paths::GetSavedDir() / "Layouts" };

		// FindInstancesOf()'s last answer
		std::string InstancesOf;
		std::vector<std::string> FoundInstances;
		double InstancesFoundAt = -1.0;

		// New Material's question
		bool bAskNewMaterialName = false;
		std::string NewMaterialName;
		std::string NewMaterialError;
		SceneViewPanel MainSceneView{ "Scene" };
		SceneViewPanel SecondarySceneView{ "Scene 2" };
		// Flies the scene views' free cameras
		EditorCameraController CameraController;
		bool bSecondaryViewShowsGameView = false;
		SceneFilePopups SceneFiles;
		PreferencesWindow PreferencesPanel;
		ThemeEditorWindow ThemeEditor;
		PlayControls Play;
		CrashCardsPanel CrashCards;

		// The games' logs and every message of their editor links
		GameLinkPanel GameLinkWindow;
		bool bShowGameLink = true;

		// The games started with Play; edits reach them from Play on
		PlaySession Games{ Editing.Scene, GameLinkWindow };
	};
}

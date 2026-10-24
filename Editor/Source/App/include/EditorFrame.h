#pragma once

#include "SceneViewTypes.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Nyx
{
	class IEditorRenderer;
	class IWindow;
	struct WindowFrameArea;
}

namespace Nyx::Editor
{
	class InputPanel;
	class MaterialWorkspace;
	class PlayControls;
	class PlaySession;
	class PreferencesWindow;
	class SceneEditor;
	class SceneFilePopups;
	class SceneViewPanel;
	class ThemeEditorWindow;
	class UISourceInspector;
	class Workspaces;
	struct EditorPreferences;
#if NYX_DEV_TOOLS
	class ImGuiDebugTools;
	class MockupOverlay;
	class PainterTestWindow;
#endif

	// The editor's part of the window's frame: the titlebar with the menus, the workspace tabs,
	// the open scene and Play, and the status bar
	class EditorFrame
	{
	public:
		// What the bars and menus show and act on. All of it is EditorLayer's, which outlives the
		// frame.
		struct Parts
		{
			// The open scene, and what New Scene and Save Scene do
			SceneEditor& Editing;
			const std::filesystem::path& CurrentScenePath;
			std::function<void()> NewScene;
			std::function<void()> SaveScene;
			SceneFilePopups& SceneFiles;

			// The windows the menus open and close
			UISourceInspector& SourceInspector;
			PreferencesWindow& PreferencesPanel;
			ThemeEditorWindow& ThemeEditor;
			MaterialWorkspace& Materials;
			bool& bAssetBrowserVisible;
			InputPanel& InputWindow;
			bool& bShowGameLink;
			SceneViewPanel& SecondarySceneView;
			bool& bSecondaryViewShowsGameView;
#if NYX_DEV_TOOLS
			ImGuiDebugTools& DebugTools;
			PainterTestWindow& PainterTest;
			MockupOverlay& Mockup;
#endif

			// The workspace tabs, and Play
			Workspaces& Layouts;
			PlayControls& Play;
			PlaySession& Games;
			EditorPreferences& Preferences;
		};

		explicit EditorFrame(Parts parts);

		// Draws the titlebar and the status bar into the window's frame (IWindow::SetFrameContent())
		// until Detach()
		void Attach(Nyx::IWindow& window, Nyx::IEditorRenderer& renderer);
		void Detach();

		// What the status bar says about the last scene saved, opened or made
		void SetSceneFileStatus(std::string text, bool bFailed);

	private:
		// The titlebar, one row: the logo, the menus, the open scene and Play. The status bar: the
		// last scene saved or opened, errors logged, the graphics API and the frame rate.
		void DrawTitlebar(const Nyx::WindowFrameArea& area);
		void DrawStatusBar(const Nyx::WindowFrameArea& area);

		// New, Save, Save As and Load, in the File menu and the scene's own
		void DrawSceneMenuItems();

		// The titlebar's workspace tabs
		void DrawWorkspaceTabs(const Nyx::WindowFrameArea& area);

		Parts Editor;
		Nyx::IWindow* Window = nullptr;
		Nyx::IEditorRenderer* Renderer = nullptr;

		// The last scene saved, opened or made, and whether that failed
		std::string SceneFileStatus;
		bool bSceneFileStatusFailed = false;
		// What the scene views show of their culling (Debug > Culling Bounds, Freeze Culling)
		Nyx::ESceneViewCullingDebug SceneCullingDebug = Nyx::ESceneViewCullingDebug::Off;
	};
}

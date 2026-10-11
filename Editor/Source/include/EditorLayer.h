#pragma once

#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "BuiltinAssetResolver.h"
#include "CrashCardsPanel.h"
#include "DetailsPanel.h"
#include "EditorAssetActivationContext.h"
#include "EditorPreferences.h"
#include "GameLinkPanel.h"
#include "ImGuiDebugTools.h"
#include "Layer.h"
#include "PlayControls.h"
#include "PlaySession.h"
#include "PreferencesWindow.h"
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneOutlinerPanel.h"
#include "SceneViewPanel.h"
#include "UISourceInspector.h"
#include "Window.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx::Editor
{
	// The whole editor as one layer of the application. It owns the scene being edited
	// (SceneEditor), the panels that show it, and Play (PlaySession); it draws the titlebar menu
	// and opens, saves and replaces scenes.
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

	private:
		// File, Window and Debug menus, Play/Stop, and the scene name, shown in the window's titlebar.
		void DrawTitlebarMenu(float buttonHeight);

		void ApplyPendingPickResults();

		// Called before destroying the old registry, once its replacement is ready.
		void ForgetPreviousScene();

	private:
		Nyx::IWindow* Window = nullptr;
		Nyx::IRenderer* Renderer = nullptr;

		// Turns the asset paths in components into the renderer's assets; created in OnAttach.
		std::unique_ptr<Nyx::Engine::BuiltinAssetResolver> AssetResolver;

		std::unique_ptr<EditorAssetActivationContext> AssetActivationContext;
		AssetDatabase AssetDb;
		std::filesystem::path CurrentScenePath;

		SceneEditor Editing;

		EditorPreferences Preferences;
		ImGuiDebugTools DebugTools;
		UISourceInspector SourceInspector;

		// Panels and windows
		AssetBrowserPanel AssetBrowser;
		bool bAssetBrowserVisible = true;
		SceneOutlinerPanel Outliner;
		DetailsPanel Details;
		SceneViewPanel MainSceneView{ "Scene" };
		SceneViewPanel SecondarySceneView{ "Scene 2" };
		bool bSecondaryViewShowsGameView = false;
		SceneFilePopups SceneFiles;
		PreferencesWindow PreferencesPanel;
		PlayControls Play;
		CrashCardsPanel CrashCards;

		// The games' logs and every message of their editor links
		GameLinkPanel GameLinkWindow;
		bool bShowGameLink = true;

		// The games started with Play; edits reach them from Play on
		PlaySession Games{ Editing.Scene, GameLinkWindow };
	};
}
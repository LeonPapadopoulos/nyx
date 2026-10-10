#pragma once

#include "Renderer.h"
#include "SceneDocument.h"
#include "TransformGizmo.h"
#include "Extent2D.h"
#include "InspectorDrawContext.h"
#include "ReflectedTransactionSystem.h"
#include "EditorTransactionSubscriber.h"
#include "SceneEntityTransactionDomain.h"
#include "MeshRendererComponent.h"
#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "BuiltinAssetResolver.h"
#include "EditorAssetActivationContext.h"
#include "Layer.h"
#include "ChildProcess.h"
#include "EditorLink.h"
#include "GameLinkPanel.h"
#include "GameLinkSubscriber.h"
#include "NetConnection.h"
#include "EditorPreferences.h"
#include "ImGuiDebugTools.h"
#include "UISourceInspector.h"
#include "Window.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <optional>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx::Editor
{
	// The whole editor as one layer of the application: scene, panels and the titlebar menu.
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
		void ToggleAssetBrowser()
		{
			bAssetBrowserVisible = !bAssetBrowserVisible;
		}
		void RequestLoadScenePopup();
		void RequestSaveSceneAsPopup();

		void ResolveMeshRendererAssets(Nyx::Engine::MeshRendererComponent& component);
		void ResolveSceneRuntimeAssets();

		std::string GetCurrentSceneDisplayName() const;

	private:
		static void MapSceneImageMouseToPickPixel(
			const ImVec2& imageMin,
			const ImVec2& imageSize,
			const ImVec2& mousePos,
			const Nyx::Extent2D& extent,
			uint32_t& outPickX,
			uint32_t& outPickY);

		void TickScene(float deltaTime);

		// File, Window and Debug menus, Play/Stop, and the scene name, shown in the window's titlebar.
		void DrawTitlebarMenu(float buttonHeight);

		void DrawSceneOutliner();
		void DrawDetailsPanel();
		void DrawSceneViews();
		void DrawSceneViewWindow(const char* title, uint64_t sceneViewId, bool& bOpen);
		void DrawSceneFilePopups();
		void DrawSourceTools();
		void OpenUISource(SourceLocation location);

		void SpawnTestScene();

		void ApplyPendingPickResults();

		void HandleUndoRedoHotkeys();

		// Called before destroying the old registry, once its replacement is ready.
		void ForgetPreviousScene();

		// Play and Stop: the open scene runs in NyxGame, as a separate program. Stop asks the game
		// to quit, and ends it if it doesn't within GameQuitTimeLimit.
		void StartGame();
		void StopGame();
		void EndGameNow();
		void StopGameBeforeEditorCloses();
		void CheckWhetherGameExited();

		// The editor link: accepts the game's connection, says Hello and checks the game's
		void UpdateGameLink();
		void HandleGameLinkMessages();

		// For the Stop button's tooltip, e.g. "connected to NyxGame (process 1234)"
		std::string GetGameLinkStatus() const;

	private:
		std::unique_ptr<Nyx::Editor::EditorAssetActivationContext> AssetActivationContext;
		Nyx::Editor::AssetDatabase AssetDb;
		Nyx::Editor::AssetBrowserPanel AssetBrowser;
		std::filesystem::path CurrentScenePath;

		bool bOpenLoadScenePopup = false;
		bool bOpenSaveSceneAsPopup = false;
		std::array<char, 256> SaveSceneAsBuffer{};

	private:
		Nyx::IWindow* Window = nullptr;
		Nyx::IRenderer* Renderer = nullptr;

		// Turns the asset paths in components into the renderer's assets; created in OnAttach.
		std::unique_ptr<Nyx::Engine::BuiltinAssetResolver> AssetResolver;

		Nyx::SceneDocument ActiveScene;
		uint64_t SceneRevision = 0;

		uint64_t MainSceneViewId = 0;
		uint64_t SecondarySceneViewId = 0;

		TransformGizmo TransformGizmoInstance;

		Nyx::Editor::SceneEntityTransactionDomain SceneEntityDomain;
		Nyx::Editor::TransactionSystem Transactions;
		Nyx::Editor::EditorTransactionContext TransactionContext;
		Nyx::Editor::InspectorDrawContext DetailsPanelContext;

		bool bShowSceneView = true;
		bool bShowSecondarySceneView = true;
		bool bSecondaryViewShowsGameView = false;
		bool bShowSceneOutliner = true;
		bool bShowDetailsPanel = true;
		bool bAssetBrowserVisible = true;

		Nyx::Editor::EditorTransactionSubscriber TransactionSubscriber;

		EditorPreferences Preferences;
		ImGuiDebugTools DebugTools;
		UISourceInspector SourceInspector;
		std::future<std::string> PendingSourceOpen;
		std::string SourceNavigationStatus;
		bool bShowPreferences = false;

		// The game started with Play
		Nyx::ChildProcess GameProcess;

		// Whether the game was running at the last check, to notice when it exits by itself
		bool bGameRunning = false;

		// Set in Play's right-click menu: the game waits at startup until a debugger is attached
		bool bGameWaitsForDebugger = false;

		// The game connects back to the editor on this port, passed to it as --editor-port.
		// Listens from the first Play on.
		Nyx::Net::Listener GameLinkListener;

		// The editor link to the game started with Play, once the game said Hello
		std::unique_ptr<Nyx::Engine::EditorLink> GameLink;

		// Connections that haven't said Hello yet. The first whose Hello comes from the game's
		// process becomes GameLink, so a program that connects and stays silent can't keep the
		// game out.
		std::vector<std::unique_ptr<Nyx::Engine::EditorLink>> GameLinkCandidates;

		// Why the last link of this play session ended, for the Stop button's tooltip
		std::string GameLinkCloseReason;

		// Set while the game was asked to quit: when it gets ended instead
		std::optional<std::chrono::steady_clock::time_point> GameQuitDeadline;
		static constexpr std::chrono::seconds GameQuitTimeLimit{ 3 };

		// The game's log and every message of the editor link
		GameLinkPanel GameLinkWindow;
		bool bShowGameLink = true;

		// Turns edits, undo and redo into messages for the game, from Play on; sent once linked
		Nyx::Editor::GameLinkSubscriber GameEdits{ ActiveScene };
	};
}
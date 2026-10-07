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
#include "Window.h"

#include <array>
#include <filesystem>

namespace Nyx::Editor
{
	// The whole editor as one layer of the application: scene, panels and the titlebar menu.
	class EditorLayer : public Nyx::Engine::ILayer
	{
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

		// File and Window menus plus the scene name, shown in the window's titlebar.
		void DrawTitlebarMenu(float buttonHeight);

		void DrawSceneOutliner();
		void DrawDetailsPanel();
		void DrawSceneViews();
		void DrawSceneViewWindow(const char* title, uint64_t sceneViewId, bool& bOpen);
		void DrawSceneFilePopups();

		void SpawnTestScene();

		void ApplyPendingPickResults();

		void HandleUndoRedoHotkeys();

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
	};
}
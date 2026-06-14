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

namespace Nyx::Editor
{
	class EditorLayer
	{
	public:
		explicit EditorLayer(Nyx::IRenderer& inRenderer);

		void Initialize();
		void Shutdown();

		void Tick(float deltaTime);
		float ComputeDeltaTime();

		void DrawPanels();

	private:
		static void MapSceneImageMouseToPickPixel(
			const ImVec2& imageMin,
			const ImVec2& imageSize,
			const ImVec2& mousePos,
			const Nyx::Extent2D& extent,
			uint32_t& outPickX,
			uint32_t& outPickY);

		void TickScene(float deltaTime);

		void DrawSceneOutliner();
		void DrawDetailsPanel();
		void DrawSceneViews();
		void DrawSceneViewWindow(const char* title, uint64_t sceneViewId, bool& bOpen);

		void SpawnTestScene();

		void ApplyPendingPickResults();

		void HandleUndoRedoHotkeys();

		void ResolveMeshRendererAssets(Nyx::Engine::MeshRendererComponent& component);

	private:
		Nyx::IRenderer* Renderer = nullptr;

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
		bool bShowSceneOutliner = true;
		bool bShowDetailsPanel = true;

		double LastFrameTimeSeconds = 0.0;

		Nyx::Editor::EditorTransactionSubscriber TransactionSubscriber;
	};
}
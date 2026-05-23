#pragma once

#include "Renderer.h"
#include "SceneDocument.h"

namespace Nyx::Editor
{
	class EditorLayer
	{
	public:
		explicit EditorLayer(Nyx::IRenderer& inRenderer);

		void Initialize();
		void Shutdown();

		void DrawPanels();

	private:
		float ComputeDeltaTime();
		void TickScene(float deltaTime);

		void DrawSceneOutliner();
		void DrawDetailsPanel();
		void DrawSceneViews();
		void DrawSceneViewWindow(const char* title, uint64_t sceneViewId, bool& bOpen);

		void SpawnTestScene();

	private:
		Nyx::IRenderer* Renderer = nullptr;

		Nyx::SceneDocument ActiveScene;

		uint64_t MainSceneViewId = 0;
		uint64_t SecondarySceneViewId = 0;

		bool bShowSceneView = true;
		bool bShowSecondarySceneView = true;
		bool bShowSceneOutliner = true;
		bool bShowDetailsPanel = true;

		double LastFrameTimeSeconds = 0.0;
	};
}
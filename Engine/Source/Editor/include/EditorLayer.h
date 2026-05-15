#pragma once
#include "SceneDocument.h"
#include "Renderer.h"

namespace Nyx::Editor
{
	class EditorLayer
	{
	public:
		void Initialize();
		void Shutdown();
		void DrawUserInterface();

	private:
		void DrawSceneOutliner();
		void SpawnTestScene();

	private:
		Nyx::SceneDocument ActiveScene;
		Nyx::IRenderer* Renderer = nullptr;

		uint64_t MainSceneViewId = 0;
		uint64_t SecondarySceneViewId = 0;
	};
}
#pragma once

#include "Extent2D.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Editor
{
	class EditorCameraController;
	class SceneEditor;
	struct EditorPreferences;

	// A window that shows one of the renderer's scene views ("Scene", "Scene 2"), with the
	// transform gizmo, and selects what is clicked in it. With ambilight on, the view's blurred
	// picture glows onto the editor around it.
	class SceneViewPanel
	{
	public:
		explicit SceneViewPanel(std::string title)
			: Title(std::move(title))
		{
		}

		// Creates the panel's scene view, or uses one the caller made (tests)
		void CreateView(Nyx::IRenderer& renderer);
		void UseView(Nyx::IRenderer& renderer, uint64_t sceneViewId);
		void DestroyView();

		uint64_t GetViewId() const
		{
			return SceneViewId;
		}

		// Also tells the camera controller whether the view is hovered or focused, and where it is
		void Draw(SceneEditor& editing, const EditorPreferences& preferences, EditorCameraController& cameraController);

		// Selects the entity of a click the renderer has finished picking. Before drawing, so the
		// panels see the new selection in the same frame.
		void ApplyPickResult(SceneEditor& editing);

		// Shows the scene the way the game sees it: through its primary camera, without editor
		// overlays. Otherwise the view has its own free camera.
		void SetShowsGameView(bool bShowsGameView);

		static void MapImageMouseToPickPixel(
			const ImVec2& imageMin,
			const ImVec2& imageSize,
			const ImVec2& mousePos,
			const Nyx::Extent2D& extent,
			uint32_t& outPickX,
			uint32_t& outPickY);

		bool bOpen = true;

	private:
		std::string Title;
		Nyx::IRenderer* Renderer = nullptr;
		uint64_t SceneViewId = 0;
		bool bShowingGameView = false;
		// A material dropped on the view, given to the entity the pick finds there
		std::string DroppedMaterial;
	};
}

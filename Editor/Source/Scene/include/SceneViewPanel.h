#pragma once

#include "Extent2D.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Nyx
{
	class IEditorRenderer;
}

namespace Nyx::Editor
{
	class AssetTypeRegistry;
	class EditorCameraController;
	class SceneEditor;
	struct EditorPreferences;

	// A window that shows one of the renderer's scene views, with the transform gizmo, and selects
	// what is clicked in it. With ambilight on, the view's blurred picture glows onto the editor
	// around it. The editor has any number of them (SceneViewSet), each numbered: "Scene",
	// "Scene 2", "Scene 3"...
	class SceneViewPanel
	{
	public:
		explicit SceneViewPanel(int number)
			: Number(number)
			, Title(GetTitle(number))
		{
		}

		// "Scene" for the first, "Scene <number>" for the others: the window's name, which ImGui
		// keeps its place in the layout by
		static std::string GetTitle(int number);

		int GetNumber() const
		{
			return Number;
		}

		const std::string& GetTitle() const
		{
			return Title;
		}

		// Creates the panel's scene view, or uses one the caller made (tests)
		void CreateView(Nyx::IEditorRenderer& renderer);
		void UseView(Nyx::IEditorRenderer& renderer, uint64_t sceneViewId);
		void DestroyView();

		uint64_t GetViewId() const
		{
			return SceneViewId;
		}

		// The types of the assets dropped on the view; without them, nothing can be dropped
		void SetAssetTypes(const AssetTypeRegistry* types)
		{
			AssetTypes = types;
		}

		// Also tells the camera controller whether the view is hovered or focused, and where it is.
		// With bCanClose, the window can be closed (bOpen).
		void Draw(SceneEditor& editing, const EditorPreferences& preferences, EditorCameraController& cameraController, bool bCanClose = true);

		// Docks the window into the dock node `dockId` the next time it is drawn, e.g. a new view
		// beside the first
		void DockNextDraw(unsigned int dockId)
		{
			PendingDockId = dockId;
		}

		// The dock node the window was in when drawn last; 0 when floating or not drawn yet
		unsigned int GetDockId() const
		{
			return DockId;
		}

		// Selects the entity of a click the renderer has finished picking, or gives it to the entity
		// reference whose eyedropper waits (InspectorDrawContext::EntityPick). Before drawing, so the
		// panels see it in the same frame.
		void ApplyPickResult(SceneEditor& editing);

		// Shows the scene the way the game sees it: through its primary camera, without editor
		// overlays. Otherwise the view has its own free camera.
		void SetShowsGameView(bool bShowsGameView);
		bool IsShowingGameView() const
		{
			return bShowingGameView;
		}

		static void MapImageMouseToPickPixel(
			const ImVec2& imageMin,
			const ImVec2& imageSize,
			const ImVec2& mousePos,
			const Nyx::Extent2D& extent,
			uint32_t& outPickX,
			uint32_t& outPickY);

		// False once its window was closed
		bool bOpen = true;

	private:
		int Number = 1;
		std::string Title;
		unsigned int PendingDockId = 0;
		unsigned int DockId = 0;
		Nyx::IEditorRenderer* Renderer = nullptr;
		uint64_t SceneViewId = 0;
		const AssetTypeRegistry* AssetTypes = nullptr;
		bool bShowingGameView = false;
		// A material dropped on the view, given to the entity the pick finds there
		std::string DroppedMaterial;
	};
}

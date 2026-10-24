#pragma once

#include "SceneViewPanel.h"
#include "SceneViewTypes.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

	// The editor's scene views: any number of SceneViewPanels, each with a scene view of its own in
	// the renderer and its own free camera. Window > New Scene View opens one more; any can be
	// closed but the last. The layout files keep which are open, with each one's camera and
	// whether it shows the game (Write(), Read()).
	class SceneViewSet
	{
	public:
		// Opens the first view, "Scene"
		void Attach(Nyx::IEditorRenderer& renderer, const AssetTypeRegistry* assetTypes);
		// Uses views the caller made, numbered from 1, instead (tests)
		void UseViews(Nyx::IEditorRenderer& renderer, std::span<const uint64_t> sceneViewIds);
		// Destroys every view
		void Detach();

		// One more view, numbered with the lowest number free, looking from where the first one
		// looks, and docked beside it when drawn
		SceneViewPanel& Open();

		// Before drawing: the views' finished picks (SceneViewPanel::ApplyPickResult())
		void ApplyPickResults(SceneEditor& editing);

		// Draws the views, then closes those whose window was closed; nothing while hidden
		void Draw(SceneEditor& editing, const EditorPreferences& preferences, EditorCameraController& cameraController);

		// Whether the views are drawn: not in the Materials workspace
		bool bShown = true;

		// What every scene view of the renderer shows of its culling and its shadows (the Debug
		// menu), views opened later too
		void SetCullingDebug(Nyx::ESceneViewCullingDebug mode);
		Nyx::ESceneViewCullingDebug GetCullingDebug() const { return CullingDebug; }
		void SetShadowDebug(Nyx::ESceneViewShadowDebug mode);
		Nyx::ESceneViewShadowDebug GetShadowDebug() const { return ShadowDebug; }

		const std::vector<std::unique_ptr<SceneViewPanel>>& GetViews() const { return Views; }
		// The views' windows, e.g. for the default layout to dock
		std::vector<std::string> GetTitles() const;

		// The views' section of the layout files: a line for each view, in order,
		// "View=<number>,<game view: 0 or 1>,<camera position x,y,z>,<camera rotation x,y,z>"
		std::vector<std::string> Write() const;
		// Replaces the open views with the section's, e.g. as the editor starts; without any line
		// it can read, the views stay as they are
		void Read(const std::vector<std::string>& lines);

	private:
		// A view numbered `number`, with a new scene view, the debug views chosen
		SceneViewPanel& Add(int number);
		void AddPanel(int number, uint64_t sceneViewId);

		Nyx::IEditorRenderer* Renderer = nullptr;
		const AssetTypeRegistry* AssetTypes = nullptr;
		// The panels stay where they are while views are opened and closed
		std::vector<std::unique_ptr<SceneViewPanel>> Views;

		Nyx::ESceneViewCullingDebug CullingDebug = Nyx::ESceneViewCullingDebug::Off;
		Nyx::ESceneViewShadowDebug ShadowDebug = Nyx::ESceneViewShadowDebug::Off;
	};
}

#pragma once

#include "AssetResolver.h"
#include "Entity.h"
#include "Layer.h"
#include "SceneSerializationTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Nyx
{
	class IRenderer;
	class IEditorRenderer;
}

namespace Nyx::Game
{
	// The whole game for now: loads one scene and shows it through the scene's primary camera
	// (or the renderer's default camera if it has none), filling the window.
	class GameLayer : public Nyx::ILayer
	{
	public:
		explicit GameLayer(std::filesystem::path scenePath);

		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnUI() override;

		// The world the game runs, e.g. for the editor's live edits. It stays at its address.
		Nyx::Registry& GetWorld()
		{
			return World;
		}

		// What components need after they were loaded or changed, e.g. to load the mesh a
		// MeshRenderer refers to. Valid once the layer is attached.
		Nyx::ScenePostLoadContext GetPostLoadContext() const
		{
			return Nyx::ScenePostLoadContext{ AssetResolver.get() };
		}

	private:
		std::filesystem::path ScenePath;

		Nyx::IRenderer* Renderer = nullptr;
		// The same renderer's UI part, for ImGui::Image() of the game's view
		Nyx::IEditorRenderer* UIRenderer = nullptr;
		std::unique_ptr<Nyx::AssetResolver> AssetResolver;

		// All entities of the loaded scene
		Nyx::Registry World;

		uint64_t GameViewId = 0;
	};
}

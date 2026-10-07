#pragma once

#include "BuiltinAssetResolver.h"
#include "Entity.h"
#include "Layer.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Game
{
	// The whole game for now: loads one scene and shows it through the scene's primary camera
	// (or the renderer's default camera if it has none), filling the window.
	class GameLayer : public Nyx::Engine::ILayer
	{
	public:
		explicit GameLayer(std::filesystem::path scenePath);

		void OnAttach(Nyx::Engine::Application& application) override;
		void OnDetach() override;
		void OnUI() override;

	private:
		std::filesystem::path ScenePath;

		Nyx::IRenderer* Renderer = nullptr;
		std::unique_ptr<Nyx::Engine::BuiltinAssetResolver> AssetResolver;

		// All entities of the loaded scene
		Nyx::Engine::Registry World;

		uint64_t GameViewId = 0;
	};
}

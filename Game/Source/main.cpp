#include "Engine.h"
#include "GameLayer.h"
#include "Paths.h"

#include <filesystem>
#include <memory>

namespace Nyx::Game
{
	// The game application: the engine's window and frame loop, running one scene.
	class GameApplication : public Nyx::Engine::Application
	{
	public:
		explicit GameApplication(const std::filesystem::path& scenePath)
			: Application(Nyx::Engine::ApplicationSpecs{ .Window = { .Title = "Nyx Game", .bUseCustomTitlebar = false, .bShowStartupBanner = false } })
		{
			PushLayer(std::make_unique<GameLayer>(scenePath));
		}
	};
}

// Usage: NyxGame.exe [scene file]. Without a scene file, the game runs Assets/Scenes/Default.nyxscene.
Nyx::Engine::Application* Nyx::Engine::CreateApplication(int argc, char** argv)
{
	std::filesystem::path scenePath = Nyx::Paths::GetScenesDir() / "Default.nyxscene";
	if (argc > 1)
	{
		scenePath = argv[1];
	}

	return new Nyx::Game::GameApplication(scenePath);
}

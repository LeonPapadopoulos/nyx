#include "Engine.h"
#include "GameLayer.h"
#include "Paths.h"
#include "ChildProcess.h"

#include <filesystem>
#include <memory>
#include <string_view>

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

// Usage: NyxGame.exe [scene file] [--wait-for-debugger]
// - Without a scene file, the game runs Assets/Scenes/Default.nyxscene.
// - With --wait-for-debugger, the game waits at startup until a debugger is attached.
Nyx::Engine::Application* Nyx::Engine::CreateApplication(int argc, char** argv)
{
	std::filesystem::path scenePath = Nyx::Paths::GetScenesDir() / "Default.nyxscene";
	bool bWaitForDebugger = false;

	for (int i = 1; i < argc; ++i)
	{
		const std::string_view argument = argv[i];

		if (argument == "--wait-for-debugger")
		{
			bWaitForDebugger = true;
		}
		else
		{
			scenePath = argument;
		}
	}

	if (bWaitForDebugger)
	{
		LOG_INFO("Waiting until a debugger is attached (--wait-for-debugger)");
		Nyx::ChildProcess::WaitForDebugger();
	}

	return new Nyx::Game::GameApplication(scenePath);
}

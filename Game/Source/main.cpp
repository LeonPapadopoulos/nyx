#include "Engine.h"
#include "EditorLinkLayer.h"
#include "EditorLinkLogSink.h"
#include "GameLaunchOptions.h"
#include "GameLayer.h"
#include "Paths.h"
#include "ChildProcess.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Game
{
	Nyx::Engine::ApplicationSpecs MakeApplicationSpecs(const Nyx::Engine::GameLaunchOptions& options)
	{
		Nyx::Engine::ApplicationSpecs specs{ .Window = { .Title = "Nyx Game", .bUseCustomTitlebar = false, .bShowStartupBanner = false } };

		if (!options.WindowTitle.empty())
		{
			specs.Window.Title = options.WindowTitle;
		}

		if (options.WindowSize)
		{
			specs.Window.Width = options.WindowSize->Width;
			specs.Window.Height = options.WindowSize->Height;
		}

		if (options.WindowPosition)
		{
			specs.Window.Position = Nyx::WindowPosition{ options.WindowPosition->X, options.WindowPosition->Y };
		}

		return specs;
	}

	// The game application: the engine's window and frame loop, running one scene.
	class GameApplication : public Nyx::Engine::Application
	{
	public:
		// editorLink is null when the editor didn't start the game
		GameApplication(const Nyx::Engine::GameLaunchOptions& options, const std::filesystem::path& scenePath,
			std::unique_ptr<EditorLinkLayer> editorLink)
			: Application(MakeApplicationSpecs(options))
		{
			// Attaching loads the scene
			auto gameLayer = std::make_unique<GameLayer>(scenePath);
			GameLayer& game = *gameLayer;
			PushLayer(std::move(gameLayer));

			if (editorLink)
			{
				// The editor's live edits go into the world the game runs
				editorLink->SetWorld(game.GetWorld(), game.GetPostLoadContext());
				PushLayer(std::move(editorLink));
			}
		}
	};
}

// Usage: NyxGame.exe [scene file] [--wait-for-debugger] [--editor-port <port>] [--link-log <file>]
//                    [--window-size <width>x<height>] [--window-pos <x>,<y>] [--window-title <text>]
// - Without a scene file, the game runs Assets/Scenes/Default.nyxscene.
// - With --wait-for-debugger, the game waits at startup until a debugger is attached.
// - With --editor-port, the game connects back to the editor that started it (the editor link),
//   sends its log lines there and quits when the editor asks.
// - With --link-log, every message of the editor link is recorded to the file (NyxDump prints it).
// - --window-size is the size of the picture, without titlebar and borders; --window-pos is where
//   the window's outer top-left corner goes. The editor's Play All uses them to tile its games.
// GameLaunchOptions.h reads these, and the editor writes them with the same code.
Nyx::Engine::Application* Nyx::Engine::CreateApplication(int argc, char** argv)
{
	const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
	std::vector<std::string> warnings;
	const Nyx::Engine::GameLaunchOptions options = Nyx::Engine::ParseGameArguments(arguments, warnings);

	const std::filesystem::path scenePath =
		options.ScenePath.empty() ? Nyx::Paths::GetScenesDir() / "Default.nyxscene" : options.ScenePath;

	// From here on, log lines are kept for the editor, even before the link is connected
	std::unique_ptr<Nyx::Game::EditorLinkLayer> editorLink;
	if (options.EditorPort)
	{
		editorLink = std::make_unique<Nyx::Game::EditorLinkLayer>(*options.EditorPort, Nyx::Engine::EditorLinkLogSink::Install(),
			options.LinkLogPath);
	}

	// Logged once the editor can get them, too
	for (const std::string& warning : warnings)
	{
		LOG_WARNING("{0}", warning);
	}

	if (!options.EditorPort && !options.LinkLogPath.empty())
	{
		LOG_WARNING("--link-log only records together with --editor-port");
	}

	if (options.bWaitForDebugger)
	{
		LOG_INFO("Waiting until a debugger is attached (--wait-for-debugger)");
		Nyx::ChildProcess::WaitForDebugger();
	}

	return new Nyx::Game::GameApplication(options, scenePath, std::move(editorLink));
}

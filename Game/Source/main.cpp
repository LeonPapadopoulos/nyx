#include "Engine.h"
#include "EditorLinkLayer.h"
#include "GameLayer.h"
#include "Paths.h"
#include "ChildProcess.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace Nyx::Game
{
	// The game application: the engine's window and frame loop, running one scene.
	class GameApplication : public Nyx::Engine::Application
	{
	public:
		GameApplication(const std::filesystem::path& scenePath, std::optional<uint16_t> editorPort)
			: Application(Nyx::Engine::ApplicationSpecs{ .Window = { .Title = "Nyx Game", .bUseCustomTitlebar = false, .bShowStartupBanner = false } })
		{
			PushLayer(std::make_unique<GameLayer>(scenePath));

			if (editorPort)
			{
				PushLayer(std::make_unique<EditorLinkLayer>(*editorPort));
			}
		}
	};

	std::optional<uint16_t> ParsePort(std::string_view text)
	{
		unsigned int port = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
		if (error != std::errc() || end != text.data() + text.size() || port == 0 || port > 65535)
		{
			return std::nullopt;
		}

		return static_cast<uint16_t>(port);
	}
}

// Usage: NyxGame.exe [scene file] [--wait-for-debugger] [--editor-port <port>]
// - Without a scene file, the game runs Assets/Scenes/Default.nyxscene.
// - With --wait-for-debugger, the game waits at startup until a debugger is attached.
// - With --editor-port, the game connects back to the editor that started it (the editor link).
Nyx::Engine::Application* Nyx::Engine::CreateApplication(int argc, char** argv)
{
	std::filesystem::path scenePath = Nyx::Paths::GetScenesDir() / "Default.nyxscene";
	bool bWaitForDebugger = false;
	std::optional<uint16_t> editorPort;

	for (int i = 1; i < argc; ++i)
	{
		const std::string_view argument = argv[i];

		if (argument == "--wait-for-debugger")
		{
			bWaitForDebugger = true;
		}
		else if (argument == "--editor-port")
		{
			editorPort = (i + 1 < argc) ? Nyx::Game::ParsePort(argv[++i]) : std::nullopt;
			if (!editorPort)
			{
				LOG_WARNING("--editor-port needs a port number from 1 to 65535; the game runs without the editor link");
			}
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

	return new Nyx::Game::GameApplication(scenePath, editorPort);
}

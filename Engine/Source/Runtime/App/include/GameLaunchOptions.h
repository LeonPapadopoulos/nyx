#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Engine
{
	// The size of the game's picture: the window's client area, without titlebar and borders
	struct GameWindowSize
	{
		uint32_t Width = 0;
		uint32_t Height = 0;

		bool operator==(const GameWindowSize&) const = default;
	};

	// Where the window's outer top-left corner goes, in screen coordinates. Negative values reach
	// monitors left of or above the main one.
	struct GameWindowPosition
	{
		int32_t X = 0;
		int32_t Y = 0;

		bool operator==(const GameWindowPosition&) const = default;
	};

	// Everything NyxGame takes on its command line. The editor fills one in to start a game, and
	// the game reads its command line into one. Both go through the two functions below, so they
	// can't disagree about the format.
	struct GameLaunchOptions
	{
		// Empty: the game runs Assets/Scenes/Default.nyxscene
		std::filesystem::path ScenePath;

		// The game waits at startup until a debugger is attached
		bool bWaitForDebugger = false;

		// The game connects back to the editor that started it on this port (the editor link)
		std::optional<uint16_t> EditorPort;

		// Records every message of the editor link to this file; only together with EditorPort
		std::filesystem::path LinkLogPath;

		// Unset: the window's default size, or its default place chosen by the system
		std::optional<GameWindowSize> WindowSize;
		std::optional<GameWindowPosition> WindowPosition;

		// Empty: "Nyx Game"
		std::string WindowTitle;
	};

	// The command line arguments for the options, in UTF-8, without the program name, e.g.
	// { "Scene.nyxscene", "--window-size", "1280x800", "--window-pos", "0,0" }
	std::vector<std::string> MakeGameArguments(const GameLaunchOptions& options);

	// Reads the arguments (UTF-8, without the program name) as MakeGameArguments() writes them. An
	// option with a missing or malformed value is left unset, and outWarnings says why, so the
	// game can still run with its default.
	GameLaunchOptions ParseGameArguments(const std::vector<std::string_view>& arguments, std::vector<std::string>& outWarnings);
}

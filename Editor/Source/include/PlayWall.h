#pragma once

#include "EditorPreferences.h"
#include "GameLaunchOptions.h"

#include <string>
#include <vector>

// Play All: where each game's window goes, so they sit side by side on the screen.
namespace Nyx::Editor
{
	// A rectangle on the screen, in physical pixels (both the editor and the game are DPI aware)
	struct ScreenRect
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;

		bool operator==(const ScreenRect&) const = default;
	};

	// What a game window's frame adds around its picture, on each side. Measured on Windows 11 at
	// 100% scale: a 640x400 picture gets a window rectangle of 656x439, of which 642x432 is visible.
	struct WindowFrame
	{
		// Titlebar and borders that can be seen
		int Left = 1;
		int Top = 31;
		int Right = 1;
		int Bottom = 1;

		// The invisible resize border around that, which Windows counts in the window's rectangle.
		// NyxGame's --window-pos places the rectangle, so tiles are shifted by it.
		int InvisibleLeft = 7;
		int InvisibleTop = 0;
		int InvisibleRight = 7;
		int InvisibleBottom = 7;
	};

	struct PlayWallScreen
	{
		// The part of the monitor not covered by the taskbar
		ScreenRect WorkArea;
		WindowFrame Frame;
	};

	struct PlayWallTile
	{
		// For NyxGame's --window-pos and --window-size
		Nyx::Engine::GameWindowPosition Position;
		Nyx::Engine::GameWindowSize Size;

		// The visible window, frame included, as it ends up on the screen
		ScreenRect Visible;
	};

	struct PlayWallLayout
	{
		// One per picture size, in the same order
		std::vector<PlayWallTile> Tiles;

		// 1, or less where the pictures had to shrink to fit; the same for all, so they keep their
		// shapes and their sizes relative to each other
		float Scale = 1.0f;

		// False if even at MinScale they don't fit; then the last rows go past the bottom
		bool bFits = true;
	};

	// Below this, pictures are too small to judge anything by
	constexpr float MinPlayWallScale = 0.1f;

	// Tiles windows for the picture sizes in rows, left to right, then top to bottom, starting at
	// the work area's top-left corner, with their visible frames touching. Shrinks all pictures by
	// the same factor if needed (never enlarges them).
	PlayWallLayout ArrangePlayWall(const std::vector<Nyx::Engine::GameWindowSize>& pictureSizes, const PlayWallScreen& screen);

	// The work area of the monitor the window (an HWND) is on, and the frame of a game window at
	// that monitor's scale. Without a window, the main monitor's.
	PlayWallScreen GetPlayWallScreen(void* nativeWindow);

	// One game to start
	struct PlannedGame
	{
		// From the setup, made unique among the games, e.g. "Steam Deck" or "Steam Deck (2)"
		std::string Name;

		// Window size, place and title; the caller adds the scene and the editor port
		Nyx::Engine::GameLaunchOptions Options;

		bool bConsoleWindow = true;
	};

	struct GamePlan
	{
		std::vector<PlannedGame> Games;

		// As in PlayWallLayout; 1 for Play
		float Scale = 1.0f;
		bool bFits = true;
	};

	// Play: one game with the first setup's size, placed by the system, with its console window.
	// Play All: one game per setup marked for it, tiled on the screen, with console windows as the
	// preferences say. Each title names its setup, and says so if the picture had to shrink.
	GamePlan PlanGames(const EditorPreferences& preferences, bool bPlayAll, const PlayWallScreen& screen);
}

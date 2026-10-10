#include "PlayWall.h"

#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace
{
	using namespace Nyx::Editor;

	Nyx::Engine::GameWindowSize ScaleSize(const Nyx::Engine::GameWindowSize& size, float scale)
	{
		return Nyx::Engine::GameWindowSize{
			(std::max)(1u, static_cast<uint32_t>(std::floor(static_cast<float>(size.Width) * scale))),
			(std::max)(1u, static_cast<uint32_t>(std::floor(static_cast<float>(size.Height) * scale))),
		};
	}

	// Places the pictures at one scale; bFits says whether all are inside the work area
	PlayWallLayout PlaceAtScale(const std::vector<Nyx::Engine::GameWindowSize>& pictureSizes, const PlayWallScreen& screen, float scale)
	{
		const ScreenRect& area = screen.WorkArea;
		const WindowFrame& frame = screen.Frame;

		PlayWallLayout layout;
		layout.Scale = scale;

		int x = area.X;
		int y = area.Y;
		int rowHeight = 0;

		for (const Nyx::Engine::GameWindowSize& pictureSize : pictureSizes)
		{
			const Nyx::Engine::GameWindowSize size = ScaleSize(pictureSize, scale);
			const int visibleWidth = static_cast<int>(size.Width) + frame.Left + frame.Right;
			const int visibleHeight = static_cast<int>(size.Height) + frame.Top + frame.Bottom;

			// The next row, unless this is the first window of its row
			if (x > area.X && x + visibleWidth > area.X + area.Width)
			{
				x = area.X;
				y += rowHeight;
				rowHeight = 0;
			}

			PlayWallTile tile;
			tile.Size = size;
			tile.Visible = ScreenRect{ x, y, visibleWidth, visibleHeight };
			tile.Position = Nyx::Engine::GameWindowPosition{ x - frame.InvisibleLeft, y - frame.InvisibleTop };
			layout.Tiles.push_back(tile);

			layout.bFits = layout.bFits && x + visibleWidth <= area.X + area.Width && y + visibleHeight <= area.Y + area.Height;

			x += visibleWidth;
			rowHeight = (std::max)(rowHeight, visibleHeight);
		}

		return layout;
	}
}

namespace Nyx::Editor
{
	PlayWallLayout ArrangePlayWall(const std::vector<Nyx::Engine::GameWindowSize>& pictureSizes, const PlayWallScreen& screen)
	{
		PlayWallLayout layout = PlaceAtScale(pictureSizes, screen, 1.0f);
		if (layout.bFits)
		{
			return layout;
		}

		PlayWallLayout smallest = PlaceAtScale(pictureSizes, screen, MinPlayWallScale);
		if (!smallest.bFits)
		{
			return smallest;
		}

		// The largest scale that fits. Fitting doesn't always get easier as the scale goes down
		// (rows break differently), but close to always; the search keeps the best that fit.
		float fits = MinPlayWallScale;
		float tooLarge = 1.0f;
		layout = smallest;
		for (int i = 0; i < 20; ++i)
		{
			const float scale = (fits + tooLarge) * 0.5f;
			PlayWallLayout candidate = PlaceAtScale(pictureSizes, screen, scale);
			if (candidate.bFits)
			{
				fits = scale;
				layout = std::move(candidate);
			}
			else
			{
				tooLarge = scale;
			}
		}

		return layout;
	}

	GamePlan PlanGames(const EditorPreferences& preferences, bool bPlayAll, const PlayWallScreen& screen)
	{
		std::vector<const PlaySetup*> setups;
		for (const PlaySetup& setup : preferences.PlaySetups)
		{
			if (!bPlayAll || setup.bInPlayAll)
			{
				setups.push_back(&setup);
			}

			// Play uses the first setup only
			if (!bPlayAll)
			{
				break;
			}
		}

		std::vector<Nyx::Engine::GameWindowSize> sizes;
		for (const PlaySetup* setup : setups)
		{
			sizes.push_back(Nyx::Engine::GameWindowSize{ setup->Width, setup->Height });
		}

		GamePlan plan;
		PlayWallLayout layout;
		if (bPlayAll)
		{
			layout = ArrangePlayWall(sizes, screen);
			plan.Scale = layout.Scale;
			plan.bFits = layout.bFits;
		}

		std::vector<std::string> names;
		for (size_t i = 0; i < setups.size(); ++i)
		{
			const PlaySetup& setup = *setups[i];

			// The Game Link window and the recordings tell games apart by name
			const std::string baseName = setup.Name.empty() ? "Game" : setup.Name;
			std::string name = baseName;
			for (int number = 2; std::find(names.begin(), names.end(), name) != names.end(); ++number)
			{
				name = baseName + " (" + std::to_string(number) + ")";
			}
			names.push_back(name);

			PlannedGame& game = plan.Games.emplace_back();
			game.Name = name;
			game.Options.WindowTitle = "Nyx Game - " + name;

			if (!bPlayAll)
			{
				game.Options.WindowSize = sizes[i];
				continue;
			}

			const PlayWallTile& tile = layout.Tiles[i];
			game.Options.WindowSize = tile.Size;
			game.Options.WindowPosition = tile.Position;
			game.bConsoleWindow = preferences.bConsoleWindowsInPlayAll;

			if (tile.Size != sizes[i])
			{
				game.Options.WindowTitle += " (" + std::to_string(sizes[i].Width) + "x" + std::to_string(sizes[i].Height) +
					" shown at " + std::to_string(tile.Size.Width) + "x" + std::to_string(tile.Size.Height) + ")";
			}
		}

		return plan;
	}

	PlayWallScreen GetPlayWallScreen(void* nativeWindow)
	{
		HWND window = static_cast<HWND>(nativeWindow);
		const HMONITOR monitor = window ? ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
										: ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

		PlayWallScreen screen;

		MONITORINFO monitorInfo{};
		monitorInfo.cbSize = sizeof(monitorInfo);
		if (::GetMonitorInfoW(monitor, &monitorInfo))
		{
			const RECT& work = monitorInfo.rcWork;
			screen.WorkArea = ScreenRect{ work.left, work.top, work.right - work.left, work.bottom - work.top };
		}

		// The frame of a resizable window with a titlebar, as GLFW makes the game's, at the scale of
		// the editor's monitor. Windows 10 and 11 draw 1 pixel of each side border; the rest of it
		// is the invisible resize border. The titlebar is all visible.
		const UINT dpi = window ? ::GetDpiForWindow(window) : ::GetDpiForSystem();
		RECT frameRect{ 0, 0, 0, 0 };
		if (::AdjustWindowRectExForDpi(&frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi))
		{
			constexpr int VisibleBorder = 1;
			const int left = -frameRect.left;
			const int right = frameRect.right;
			const int bottom = frameRect.bottom;

			screen.Frame.Left = VisibleBorder;
			screen.Frame.Right = VisibleBorder;
			screen.Frame.Bottom = VisibleBorder;
			screen.Frame.Top = -frameRect.top;
			screen.Frame.InvisibleLeft = (std::max)(left - VisibleBorder, 0);
			screen.Frame.InvisibleRight = (std::max)(right - VisibleBorder, 0);
			screen.Frame.InvisibleBottom = (std::max)(bottom - VisibleBorder, 0);
			screen.Frame.InvisibleTop = 0;
		}

		return screen;
	}
}

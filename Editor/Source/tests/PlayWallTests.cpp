// Play setups and Play All: how setups are saved, where Play All puts each game's window, and
// which games Play and Play All start.
#include "EditorPreferences.h"
#include "PlayWall.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using Nyx::Engine::GameWindowPosition;
	using Nyx::Engine::GameWindowSize;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	// A 1920x1080 monitor with a 40 pixel taskbar, and the frame measured on Windows 11 at 100%
	PlayWallScreen MakeScreen(int width = 1920, int height = 1040)
	{
		PlayWallScreen screen;
		screen.WorkArea = ScreenRect{ 0, 0, width, height };
		return screen;
	}

	void TestPreferencesRoundTrip()
	{
		EditorPreferences written;
		written.SourceEditor = ESourceEditor::VisualStudioCode;
		written.bConsoleWindowsInPlayAll = true;
		written.PlaySetups = {
			PlaySetup{ .Name = "Phone, portrait", .Width = 390, .Height = 844, .bInPlayAll = false },
			PlaySetup{ .Name = "Ultrawide", .Width = 2560, .Height = 1080, .bInPlayAll = true },
		};

		EditorPreferences read;
		read.FromText(written.ToText());
		Require(read.SourceEditor == ESourceEditor::VisualStudioCode, "The source editor was lost");
		Require(read.bConsoleWindowsInPlayAll, "Console windows in Play All was lost");
		Require(read.PlaySetups == written.PlaySetups, "The play setups differ after a round trip (a comma in a name?)");

		// Through a file too, as the editor saves them
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxPlayWallTests.ini";
		Require(written.Save(path), "Saving the preferences failed");
		EditorPreferences loaded;
		Require(loaded.Load(path) && loaded.PlaySetups == written.PlaySetups, "The play setups differ after saving and loading");
		std::filesystem::remove(path);
	}

	void TestPreferencesDefaultsAndDamage()
	{
		// Preferences from before play setups: the defaults
		EditorPreferences old;
		old.FromText("# Nyx editor user preferences\r\nSourceEditor=VisualStudio\r\n");
		Require(old.PlaySetups == GetDefaultPlaySetups(), "Without saved setups, the defaults should be used");
		Require(!old.bConsoleWindowsInPlayAll, "Console windows in Play All should be off by default");

		// A damaged line loses only its setup
		EditorPreferences damaged;
		damaged.FromText("PlaySetup=1,1280x800,Good\n"
						 "PlaySetup=1,1280x0,Zero height\n"
						 "PlaySetup=2,640x480,Bad flag\n"
						 "PlaySetup=1,640x480\n"
						 "PlaySetup=0,99999x480,Too wide\n"
						 "PlaySetup=0,800x600,Also good\n"
						 "Unknown=1\n");
		Require(damaged.PlaySetups.size() == 2 && damaged.PlaySetups[0].Name == "Good" && damaged.PlaySetups[1].Name == "Also good",
			"Only the readable setups should be kept");
		Require(!damaged.PlaySetups[1].bInPlayAll, "A setup's Play All flag was read wrong");

		// A line break in a name can't end the setting early
		EditorPreferences multiline;
		multiline.PlaySetups = { PlaySetup{ .Name = "Two\nLines", .Width = 800, .Height = 600 } };
		EditorPreferences read;
		read.FromText(multiline.ToText());
		Require(read.PlaySetups.size() == 1 && read.PlaySetups[0].Name == "Two Lines", "A line break in a name should become a space");
	}

	void TestArrangeFits()
	{
		const PlayWallScreen screen = MakeScreen();
		const PlayWallLayout layout = ArrangePlayWall({ { 640, 400 }, { 640, 400 } }, screen);
		Require(layout.bFits && layout.Scale == 1.0f, "Two small windows should fit at full size");
		Require(layout.Tiles.size() == 2, "One tile per size");

		// Visible frames touch: 640 + 1 + 1 wide, 400 + 31 + 1 high
		Require(layout.Tiles[0].Visible == ScreenRect{ 0, 0, 642, 432 }, "The first window should be at the work area's corner");
		Require(layout.Tiles[1].Visible == ScreenRect{ 642, 0, 642, 432 }, "The second window should be right of the first");

		// --window-pos places the window rectangle, which includes the invisible border
		Require(layout.Tiles[0].Position == GameWindowPosition{ -7, 0 }, "The position should allow for the invisible border");
		Require(layout.Tiles[1].Size == GameWindowSize{ 640, 400 }, "Sizes shouldn't change when they fit");
	}

	void TestArrangeRows()
	{
		// Three 800 wide windows on 1920: two in the first row, one in the second
		const PlayWallLayout layout = ArrangePlayWall({ { 800, 450 }, { 800, 450 }, { 800, 450 } }, MakeScreen());
		Require(layout.bFits && layout.Scale == 1.0f, "Three 800x450 windows should fit in two rows");
		Require(layout.Tiles[1].Visible.Y == 0 && layout.Tiles[2].Visible.X == 0 && layout.Tiles[2].Visible.Y == 482,
			"The third window should start the second row, below the first row's tallest window");

		// A work area that doesn't start at 0,0, e.g. a second monitor left of the main one
		PlayWallScreen left = MakeScreen();
		left.WorkArea.X = -1920;
		left.WorkArea.Y = 40;
		const PlayWallLayout leftLayout = ArrangePlayWall({ { 800, 450 } }, left);
		Require(leftLayout.Tiles[0].Visible.X == -1920 && leftLayout.Tiles[0].Visible.Y == 40, "Tiles should start at the work area's corner");
	}

	void TestArrangeShrinks()
	{
		// Four default setups don't fit at full size on 1920x1040
		std::vector<GameWindowSize> sizes;
		for (const PlaySetup& setup : GetDefaultPlaySetups())
		{
			sizes.push_back({ setup.Width, setup.Height });
		}

		const PlayWallScreen screen = MakeScreen();
		const PlayWallLayout layout = ArrangePlayWall(sizes, screen);
		Require(layout.bFits, "The default setups should fit when shrunk");
		Require(layout.Scale < 1.0f && layout.Scale > 0.5f, "The default setups should shrink, but not by more than half");

		for (size_t i = 0; i < sizes.size(); ++i)
		{
			const PlayWallTile& tile = layout.Tiles[i];
			const ScreenRect& v = tile.Visible;
			Require(v.X >= 0 && v.Y >= 0 && v.X + v.Width <= 1920 && v.Y + v.Height <= 1040, "A shrunk window is outside the work area");

			// Same shape, within rounding
			const float shapeBefore = static_cast<float>(sizes[i].Width) / static_cast<float>(sizes[i].Height);
			const float shapeAfter = static_cast<float>(tile.Size.Width) / static_cast<float>(tile.Size.Height);
			Require(std::abs(shapeBefore - shapeAfter) < 0.02f, "A shrunk window changed its shape");

			for (size_t j = 0; j < i; ++j)
			{
				const ScreenRect& o = layout.Tiles[j].Visible;
				const bool bOverlap = v.X < o.X + o.Width && o.X < v.X + v.Width && v.Y < o.Y + o.Height && o.Y < v.Y + v.Height;
				Require(!bOverlap, "Two windows overlap");
			}
		}

		// Shrinking is the most that fits: a little larger doesn't
		const float slightlyLarger = layout.Scale + 0.01f;
		std::vector<GameWindowSize> larger;
		for (const GameWindowSize& size : sizes)
		{
			larger.push_back({ static_cast<uint32_t>(size.Width * slightlyLarger), static_cast<uint32_t>(size.Height * slightlyLarger) });
		}
		Require(!ArrangePlayWall(larger, screen).bFits || ArrangePlayWall(larger, screen).Scale < 1.0f,
			"The windows should have been shrunk less");
	}

	void TestArrangeTooMany()
	{
		// Far too many for a tiny work area: the smallest scale, and a warning-worthy layout
		const std::vector<GameWindowSize> sizes(50, GameWindowSize{ 1920, 1080 });
		const PlayWallLayout layout = ArrangePlayWall(sizes, MakeScreen(400, 300));
		Require(!layout.bFits && layout.Scale == MinPlayWallScale, "Windows that can't fit should get the smallest scale");
		Require(layout.Tiles.size() == sizes.size(), "Every window still gets a place");
	}

	void TestPlanPlay()
	{
		EditorPreferences preferences;
		preferences.bConsoleWindowsInPlayAll = false;

		const GamePlan plan = PlanGames(preferences, false, MakeScreen());
		Require(plan.Games.size() == 1, "Play should start one game");
		const PlannedGame& game = plan.Games[0];
		Require(game.Name == "720p" && game.Options.WindowTitle == "Nyx Game - 720p", "Play should use the first setup's name");
		Require(game.Options.WindowSize == GameWindowSize{ 1280, 720 }, "Play should use the first setup's size");
		Require(!game.Options.WindowPosition, "Play should let the system place the window");
		Require(game.bConsoleWindow, "Play should always show the console");

		// Play uses the first setup even if it isn't marked for Play All
		preferences.PlaySetups[0].bInPlayAll = false;
		Require(PlanGames(preferences, false, MakeScreen()).Games.size() == 1, "Play shouldn't depend on Play All's marks");
	}

	void TestPlanPlayAll()
	{
		EditorPreferences preferences;
		preferences.PlaySetups = {
			PlaySetup{ .Name = "Deck", .Width = 1280, .Height = 800, .bInPlayAll = true },
			PlaySetup{ .Name = "Skipped", .Width = 640, .Height = 480, .bInPlayAll = false },
			PlaySetup{ .Name = "Deck", .Width = 640, .Height = 400, .bInPlayAll = true },
			PlaySetup{ .Name = "", .Width = 320, .Height = 200, .bInPlayAll = true },
		};

		GamePlan plan = PlanGames(preferences, true, MakeScreen(3840, 2100));
		Require(plan.Games.size() == 3, "Play All should start the marked setups only");
		Require(plan.Games[0].Name == "Deck" && plan.Games[1].Name == "Deck (2)" && plan.Games[2].Name == "Game",
			"Game names should be unique, and never empty");
		Require(plan.Scale == 1.0f && plan.Games[1].Options.WindowTitle == "Nyx Game - Deck (2)", "Titles should name the setup");
		Require(plan.Games[0].Options.WindowPosition.has_value(), "Play All should place the windows");
		Require(!plan.Games[0].bConsoleWindow, "Play All shouldn't show consoles unless asked to");

		preferences.bConsoleWindowsInPlayAll = true;
		Require(PlanGames(preferences, true, MakeScreen()).Games[0].bConsoleWindow, "Play All should show consoles when asked to");

		// On a small screen, the titles say that the pictures are shrunk
		plan = PlanGames(preferences, true, MakeScreen(1280, 700));
		Require(plan.Scale < 1.0f, "The games should shrink on a small screen");
		const std::string& title = plan.Games[0].Options.WindowTitle;
		Require(title.starts_with("Nyx Game - Deck (1280x800 shown at ") && title.ends_with(")"),
			"A shrunk game's title should say so: " + title);

		// Nothing marked: nothing to start
		for (PlaySetup& setup : preferences.PlaySetups)
		{
			setup.bInPlayAll = false;
		}
		Require(PlanGames(preferences, true, MakeScreen()).Games.empty(), "Play All without marked setups should start nothing");
	}
}

int main()
{
	try
	{
		TestPreferencesRoundTrip();
		TestPreferencesDefaultsAndDamage();
		TestArrangeFits();
		TestArrangeRows();
		TestArrangeShrinks();
		TestArrangeTooMany();
		TestPlanPlay();
		TestPlanPlayAll();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Play wall test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All play wall tests passed.\n";
	return 0;
}

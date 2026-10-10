// The game's command line: what the editor writes with MakeGameArguments() and the game reads
// with ParseGameArguments().
#include "GameLaunchOptions.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	GameLaunchOptions Parse(const std::vector<std::string>& arguments, std::vector<std::string>& outWarnings)
	{
		const std::vector<std::string_view> views(arguments.begin(), arguments.end());
		return ParseGameArguments(views, outWarnings);
	}

	// Parses arguments that are expected to give exactly one warning, which names the option
	GameLaunchOptions ParseWithOneWarning(const std::vector<std::string>& arguments, const std::string& option)
	{
		std::vector<std::string> warnings;
		GameLaunchOptions options = Parse(arguments, warnings);
		Require(warnings.size() == 1, "Expected one warning for " + option + ", got " + std::to_string(warnings.size()));
		Require(warnings[0].find(option) != std::string::npos, "The warning doesn't name " + option + ": " + warnings[0]);
		return options;
	}

	void TestDefaultsMakeNoArguments()
	{
		Require(MakeGameArguments(GameLaunchOptions{}).empty(), "Default options should make no arguments");

		std::vector<std::string> warnings;
		const GameLaunchOptions options = Parse({}, warnings);
		Require(warnings.empty(), "No arguments should give no warnings");
		Require(options.ScenePath.empty() && !options.bWaitForDebugger && !options.EditorPort && options.LinkLogPath.empty() &&
					!options.WindowSize && !options.WindowPosition && options.WindowTitle.empty(),
			"No arguments should leave every option at its default");
	}

	void TestRoundTrip()
	{
		GameLaunchOptions written;
		written.ScenePath = "C:/Projects/My Game/PlaySession.nyxscene";
		written.bWaitForDebugger = true;
		written.EditorPort = 54321;
		written.LinkLogPath = "Link Logs/Game 1.nyxlinklog";
		written.WindowSize = GameWindowSize{ 1280, 800 };
		written.WindowPosition = GameWindowPosition{ -1920, -40 };
		written.WindowTitle = "Nyx Game - Steam Deck";

		const std::vector<std::string> arguments = MakeGameArguments(written);

		std::vector<std::string> warnings;
		const GameLaunchOptions read = Parse(arguments, warnings);
		Require(warnings.empty(), "A round trip shouldn't warn");
		Require(read.ScenePath == written.ScenePath, "Scene path differs after a round trip");
		Require(read.bWaitForDebugger, "--wait-for-debugger lost in a round trip");
		Require(read.EditorPort == written.EditorPort, "Editor port differs after a round trip");
		Require(read.LinkLogPath == written.LinkLogPath, "Link log path differs after a round trip");
		Require(read.WindowSize == written.WindowSize, "Window size differs after a round trip");
		Require(read.WindowPosition == written.WindowPosition, "Window position differs after a round trip");
		Require(read.WindowTitle == written.WindowTitle, "Window title differs after a round trip");
	}

	// Paths and titles beyond ASCII, e.g. a play setup named in German: everything is UTF-8
	void TestNonAsciiRoundTrip()
	{
		GameLaunchOptions written;
		written.ScenePath = std::filesystem::path(u8"C:/Spiele/Übung ü/Größe.nyxscene");
		written.WindowTitle = reinterpret_cast<const char*>(u8"Nyx Game - Größe ✓");

		std::vector<std::string> warnings;
		const GameLaunchOptions read = Parse(MakeGameArguments(written), warnings);
		Require(warnings.empty(), "A non-ASCII round trip shouldn't warn");
		Require(read.ScenePath == written.ScenePath, "A non-ASCII scene path differs after a round trip");
		Require(read.WindowTitle == written.WindowTitle, "A non-ASCII title differs after a round trip");
	}

	void TestWindowValues()
	{
		std::vector<std::string> warnings;
		GameLaunchOptions options = Parse({ "--window-size", "1024X768", "--window-pos", "0,0" }, warnings);
		Require(warnings.empty(), "Valid window values shouldn't warn");
		Require(options.WindowSize == GameWindowSize{ 1024, 768 }, "An upper-case X should separate width and height too");
		Require(options.WindowPosition == GameWindowPosition{ 0, 0 }, "0,0 is a valid position");

		// Each is left unset, with a warning that names the option
		for (const char* size : { "0x800", "1280x0", "1280", "1280x", "x800", "1280x800x2", "-1280x800", "abcxdef", "99999x800", "1280 x 800" })
		{
			options = ParseWithOneWarning({ "--window-size", size }, "--window-size");
			Require(!options.WindowSize, std::string("Accepted the window size ") + size);
		}

		for (const char* position : { "10", "10,", ",10", "+10,10", "10;10", "1.5,2", "99999999999,0" })
		{
			options = ParseWithOneWarning({ "--window-pos", position }, "--window-pos");
			Require(!options.WindowPosition, std::string("Accepted the window position ") + position);
		}
	}

	void TestMissingAndBadValues()
	{
		// An option at the end, without its value
		for (const char* option : { "--editor-port", "--link-log", "--window-size", "--window-pos", "--window-title" })
		{
			ParseWithOneWarning({ "Scene.nyxscene", option }, option);
		}

		for (const char* port : { "0", "65536", "-1", "port" })
		{
			const GameLaunchOptions options = ParseWithOneWarning({ "--editor-port", port }, "--editor-port");
			Require(!options.EditorPort, std::string("Accepted the port ") + port);
		}

		// A bad value doesn't take the scene path with it, and the options after it still count
		std::vector<std::string> warnings;
		const GameLaunchOptions options = Parse({ "--window-size", "big", "Scene.nyxscene", "--window-title", "Wall 2" }, warnings);
		Require(warnings.size() == 1, "Only the bad window size should warn");
		Require(options.ScenePath == "Scene.nyxscene", "The scene path after a bad value was lost");
		Require(options.WindowTitle == "Wall 2", "The title after a bad value was lost");
	}

	void TestUnknownOption()
	{
		const GameLaunchOptions options = ParseWithOneWarning({ "--fullscreen", "Scene.nyxscene" }, "--fullscreen");
		Require(options.ScenePath == "Scene.nyxscene", "An unknown option shouldn't become the scene path");
	}
}

int main()
{
	try
	{
		TestDefaultsMakeNoArguments();
		TestRoundTrip();
		TestNonAsciiRoundTrip();
		TestWindowValues();
		TestMissingAndBadValues();
		TestUnknownOption();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game launch options test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game launch options tests passed.\n";
	return 0;
}

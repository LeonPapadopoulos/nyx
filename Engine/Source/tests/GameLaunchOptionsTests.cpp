// The game's command line: what the editor writes with MakeGameArguments() and the game reads
// with ParseGameArguments().
#include "GameLaunchOptions.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx;

	using Nyx::Tests::Require;

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
					!options.bInputBackground && options.InputRecordPath.empty() && options.InputReplayPath.empty() && !options.WindowSize && !options.WindowPosition && options.WindowTitle.empty() && !options.MsaaSamples,
			"No arguments should leave every option at its default");
	}

	void TestRoundTrip()
	{
		GameLaunchOptions written;
		written.ScenePath = "C:/Projects/My Game/PlaySession.nyxscene";
		written.bWaitForDebugger = true;
		written.EditorPort = 54321;
		written.LinkLogPath = "Link Logs/Game 1.nyxlinklog";
		written.bInputBackground = true;
		written.InputRecordPath = "Input Recordings/Run 1.nyxinputrec";
		written.InputReplayPath = "Bug 42.nyxinputrec";
		written.WindowSize = GameWindowSize{ 1280, 800 };
		written.WindowPosition = GameWindowPosition{ -1920, -40 };
		written.WindowTitle = "Nyx Game - Steam Deck";
		written.MsaaSamples = 8;
		written.bWindowHidden = true;
		written.bShowFrameStats = true;
		written.BenchmarkSeconds = 12.5;
		written.BenchmarkWarmupSeconds = 0.1;
		written.BenchmarkOutPath = "Benchmarks/Valley at night.csv";
		written.BenchmarkCameraPath = "Benchmarks/Valley.txt";

		const std::vector<std::string> arguments = MakeGameArguments(written);

		std::vector<std::string> warnings;
		const GameLaunchOptions read = Parse(arguments, warnings);
		Require(warnings.empty(), "A round trip shouldn't warn");
		Require(read.ScenePath == written.ScenePath, "Scene path differs after a round trip");
		Require(read.bWaitForDebugger, "--wait-for-debugger lost in a round trip");
		Require(read.EditorPort == written.EditorPort, "Editor port differs after a round trip");
		Require(read.LinkLogPath == written.LinkLogPath, "Link log path differs after a round trip");
		Require(read.bInputBackground, "--input-background lost in a round trip");
		Require(read.InputRecordPath == written.InputRecordPath, "Input record path differs after a round trip");
		Require(read.InputReplayPath == written.InputReplayPath, "Input replay path differs after a round trip");
		Require(read.WindowSize == written.WindowSize, "Window size differs after a round trip");
		Require(read.WindowPosition == written.WindowPosition, "Window position differs after a round trip");
		Require(read.WindowTitle == written.WindowTitle, "Window title differs after a round trip");
		Require(read.MsaaSamples == written.MsaaSamples, "MSAA samples differ after a round trip");
		Require(read.bWindowHidden, "--window-hidden lost in a round trip");
		Require(read.bShowFrameStats, "--frame-stats lost in a round trip");
		Require(read.BenchmarkSeconds == written.BenchmarkSeconds, "Benchmark seconds differ after a round trip");
		Require(read.BenchmarkWarmupSeconds == written.BenchmarkWarmupSeconds, "Benchmark warm-up seconds differ after a round trip");
		Require(read.BenchmarkOutPath == written.BenchmarkOutPath, "Benchmark output path differs after a round trip");
		Require(read.BenchmarkCameraPath == written.BenchmarkCameraPath, "Benchmark camera path differs after a round trip");
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

	void TestBenchmarkValues()
	{
		// The command line a benchmark is run with
		std::vector<std::string> warnings;
		GameLaunchOptions options = Parse({ "Scenes/Valley.nyxscene", "--benchmark", "10", "--benchmark-out", "valley.csv" }, warnings);
		Require(warnings.empty(), "A benchmark's command line shouldn't warn");
		Require(options.ScenePath == "Scenes/Valley.nyxscene", "The scene path of a benchmark was lost");
		Require(options.BenchmarkSeconds == 10.0, "--benchmark 10 should record 10 seconds");
		Require(options.BenchmarkOutPath == "valley.csv", "--benchmark-out was lost");
		Require(!options.BenchmarkWarmupSeconds && options.BenchmarkCameraPath.empty(), "Options not given should stay unset");

		options = Parse({ "--benchmark", "0.5", "--benchmark-warmup", "1e1" }, warnings);
		Require(warnings.empty() && options.BenchmarkSeconds == 0.5 && options.BenchmarkWarmupSeconds == 10.0, "Fractions and exponents are seconds too");

		// Each is left unset, with a warning that names the option
		for (const char* seconds : { "0", "-1", "ten", "10s", "", "1e9", "nan", "inf" })
		{
			options = ParseWithOneWarning({ "--benchmark", seconds }, "--benchmark");
			Require(!options.BenchmarkSeconds, std::string("Accepted the benchmark seconds ") + seconds);
			options = ParseWithOneWarning({ "--benchmark-warmup", seconds }, "--benchmark-warmup");
			Require(!options.BenchmarkWarmupSeconds, std::string("Accepted the warm-up seconds ") + seconds);
		}
	}

	void TestMissingAndBadValues()
	{
		// An option at the end, without its value
		for (const char* option : { "--editor-port", "--link-log", "--window-size", "--window-pos", "--window-title", "--msaa", "--benchmark", "--benchmark-warmup",
				 "--benchmark-out", "--benchmark-camera" })
		{
			ParseWithOneWarning({ "Scene.nyxscene", option }, option);
		}

		for (const char* samples : { "0", "3", "16", "-4", "four" })
		{
			const GameLaunchOptions options = ParseWithOneWarning({ "--msaa", samples }, "--msaa");
			Require(!options.MsaaSamples, std::string("Accepted the sample count ") + samples);
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
		TestBenchmarkValues();
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

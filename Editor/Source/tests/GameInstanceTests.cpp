// Several games at once, as Play starts them: real processes, each with its own editor link.
//
// The games are this program again, started with the game's command line: it then acts as a
// small stand-in for NyxGame (RunStandInGame) that needs no window or GPU. It says Hello, reports
// every message it gets as a log line, and quits when asked. A title containing "Crash" makes it
// exit with code 3 right after its Hello. The stand-ins run without console windows, which they
// don't need, and which Windows sometimes fails to create when many open and close at once.
#include "BinaryArchive.h"
#include "EditorLink.h"
#include "EditorLinkMessages.h"
#include "GameInstance.h"
#include "GameLaunchOptions.h"
#include "Log.h"
#include "NetConnection.h"
#include "Paths.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using namespace Nyx::Engine;

	constexpr int CrashExitCode = 3;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	void SendLine(EditorLink& link, const std::string& text)
	{
		LogLineMessage line;
		line.TimeMs = GetClockTimeMs();
		line.LoggerName = "APP";
		line.Text = text;
		link.Send(line);
	}

	// Writes through a null pointer. A function of its own, so the crash report has a frame to find.
	__declspec(noinline) void CrashOnPurpose()
	{
		volatile int* nowhere = nullptr;
		*nowhere = 1;
	}

	// The stand-in for NyxGame, in the started process
	int RunStandInGame(const std::vector<std::string_view>& arguments)
	{
		std::vector<std::string> warnings;
		const GameLaunchOptions options = ParseGameArguments(arguments, warnings);
		if (!options.EditorPort || !warnings.empty())
		{
			return 2;
		}

		EditorLink link(Nyx::Net::Connection::ConnectToLocalPort(*options.EditorPort), "NyxGame");
		const bool bCrash = options.WindowTitle.find("Crash") != std::string::npos;
		bool bSaidStarted = false;

		const auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (std::chrono::steady_clock::now() < giveUpAt)
		{
			link.Update();

			if (link.GetState() == EEditorLinkState::Closed)
			{
				return 4;
			}

			if (link.IsConnected() && !bSaidStarted)
			{
				if (bCrash)
				{
					// Ends without closing the link, like a crash
					return CrashExitCode;
				}

				SendLine(link, "started as " + options.WindowTitle);
				bSaidStarted = true;

				// Crashes for real, with the crash handler NyxGame installs: it sends the report
				// over the link before the process ends
				if (options.WindowTitle.find("AccessViolation") != std::string::npos)
				{
					Nyx::InstallCrashHandler(
						[&link](const Nyx::CrashReport& report)
						{
							link.Send(CrashMessage{ report });
							link.CloseGracefully("the game crashed", std::chrono::milliseconds(1000));
						});
					CrashOnPurpose();
				}
			}

			while (std::optional<Nyx::Net::Message> message = link.Receive())
			{
				if (message->Type == static_cast<uint16_t>(EEditorLinkMessage::Quit))
				{
					SendLine(link, "bye");
					link.CloseGracefully("the game is closing", std::chrono::milliseconds(1000));
					return 0;
				}

				SendLine(link, "got " + std::to_string(message->Type) + " of " + std::to_string(message->Payload.size()) + " bytes");
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		return 5;
	}

	// What each game sent as log lines, as the Game Link window would see them
	struct LogLines
	{
		std::map<std::string, std::vector<std::string>> ByGame;

		GameInstance::MessageObserver MakeObserver()
		{
			return [this](const std::string& gameName, ELinkDirection direction, const Nyx::Net::Message& message)
			{
				if (direction != ELinkDirection::Received || message.Type != static_cast<uint16_t>(EEditorLinkMessage::LogLine))
				{
					return;
				}

				BinaryReader reader;
				reader.LoadFromMemory(message.Payload);
				LogLineMessage line;
				Require(line.Read(reader), "A log line from " + gameName + " can't be read");
				ByGame[gameName].push_back(line.Text);
			};
		}

		bool Has(const std::string& gameName, const std::string& text) const
		{
			const auto it = ByGame.find(gameName);
			return it != ByGame.end() && std::find(it->second.begin(), it->second.end(), text) != it->second.end();
		}
	};

	// Updates the games like the editor's frames until the condition holds
	void UpdateUntil(std::vector<std::unique_ptr<GameInstance>>& games, const std::function<bool()>& condition, const std::string& what)
	{
		const auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (!condition())
		{
			Require(std::chrono::steady_clock::now() < giveUpAt, "Timed out waiting until " + what);

			for (const std::unique_ptr<GameInstance>& game : games)
			{
				game->Update();
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	bool IsLinked(const GameInstance& game)
	{
		return game.GetLinkStatus().starts_with("connected");
	}

	void TestSeveralGames()
	{
		const std::filesystem::path executable = Nyx::Paths::GetExecutableDir() / "NyxGameInstanceTests.exe";
		LogLines lines;

		std::vector<std::unique_ptr<GameInstance>> games;
		for (const char* name : { "Game 1", "Game 2", "Game 3 Crash" })
		{
			GameLaunchOptions options;
			options.ScenePath = "PlaySession.nyxscene";
			options.WindowTitle = name;

			games.push_back(std::make_unique<GameInstance>(name, lines.MakeObserver()));
			Require(games.back()->Start(executable, options, false), std::string("Couldn't start ") + name);
			Require(games.back()->TakesEdits(), std::string(name) + " should take edits from the start");
		}

		GameInstance& first = *games[0];
		GameInstance& second = *games[1];
		GameInstance& crashing = *games[2];
		Require(first.GetProcessId() != second.GetProcessId(), "Two games have the same process");

		// Edits made while the games are starting wait in each game's queue
		const Nyx::Net::Message edit{ static_cast<uint16_t>(EEditorLinkMessage::SetProperties), std::vector<std::byte>(10) };
		for (const std::unique_ptr<GameInstance>& game : games)
		{
			game->QueueEdits({ edit });
		}

		UpdateUntil(games,
			[&]()
			{
				// Linked, and their first log line arrived, which comes right after the link
				return IsLinked(first) && IsLinked(second) && !crashing.IsRunning() && lines.Has("Game 1", "started as Game 1") &&
					lines.Has("Game 2", "started as Game 2");
			},
			"games 1 and 2 are linked and said so, and game 3 crashed");

		Require(lines.Has("Game 1", "started as Game 1") && !lines.Has("Game 2", "started as Game 1"),
			"Each game's log lines should arrive under its own name");

		// The crash only ended its own game
		Require(!crashing.TakesEdits(), "A game that crashed shouldn't take edits");
		Require(first.IsRunning() && second.IsRunning() && first.TakesEdits() && second.TakesEdits(),
			"A crash of one game shouldn't affect the others");

		// Every edit reaches every game, in order
		const Nyx::Net::Message secondEdit{ static_cast<uint16_t>(EEditorLinkMessage::DeleteEntity), std::vector<std::byte>(8) };
		first.QueueEdits({ secondEdit });
		second.QueueEdits({ secondEdit });

		UpdateUntil(games,
			[&]()
			{
				return lines.Has("Game 1", "got 6 of 8 bytes") && lines.Has("Game 2", "got 6 of 8 bytes");
			},
			"both games got the second edit");

		for (const char* name : { "Game 1", "Game 2" })
		{
			const std::vector<std::string>& got = lines.ByGame[name];
			const auto firstEdit = std::find(got.begin(), got.end(), "got 4 of 10 bytes");
			const auto laterEdit = std::find(got.begin(), got.end(), "got 6 of 8 bytes");
			Require(firstEdit != got.end() && firstEdit < laterEdit, std::string(name) + " should get the edits in order");
		}

		// Stop asks each to quit; they say goodbye and exit by themselves, without being ended
		first.Stop();
		second.Stop();
		Require(first.IsQuitting() && second.IsQuitting(), "Stop should ask linked games to quit");
		Require(!first.TakesEdits(), "A game asked to quit shouldn't take edits");

		UpdateUntil(games,
			[&]()
			{
				return !first.IsRunning() && !second.IsRunning();
			},
			"both games quit");

		Require(lines.Has("Game 1", "bye") && lines.Has("Game 2", "bye"), "Both games should have quit by themselves");
	}

	void TestUnstartedGame()
	{
		// Before Start(), there is no game to send edits to
		GameInstance game("Unstarted", {});
		Require(!game.TakesEdits(), "A game that wasn't started shouldn't take edits");
		game.QueueEdits({ Nyx::Net::Message{ 4, std::vector<std::byte>(16) } });
		Require(!game.TakesEdits(), "Queuing shouldn't make a game take edits");
	}

	// A game that crashes sends a report with its call stack; the editor keeps it for the crash
	// card, with what it takes to start the game again. A game that quits normally leaves none.
	void TestCrashReports()
	{
		const std::filesystem::path executable = Nyx::Paths::GetExecutableDir() / "NyxGameInstanceTests.exe";
		LogLines lines;

		std::vector<std::unique_ptr<GameInstance>> games;
		for (const char* name : { "Game AccessViolation", "Game 3 Crash", "Game Fine" })
		{
			GameLaunchOptions options;
			options.ScenePath = "PlaySession.nyxscene";
			options.WindowTitle = name;
			options.WindowSize = GameWindowSize{ 640, 400 };
			games.push_back(std::make_unique<GameInstance>(name, lines.MakeObserver()));
			Require(games.back()->Start(executable, options, false), std::string("Couldn't start ") + name);
		}

		GameInstance& crashing = *games[0];
		GameInstance& exitCode = *games[1];
		GameInstance& fine = *games[2];

		UpdateUntil(games,
			[&]()
			{
				return !crashing.IsRunning() && !exitCode.IsRunning() && IsLinked(fine);
			},
			"two games crashed and one is linked");

		// The real crash: exit code, description and call stack, with symbols
		std::optional<GameCrash> crash = crashing.TakeCrash();
		Require(crash.has_value(), "A crashed game left no crash");
		Require(!crashing.TakeCrash().has_value(), "A crash should be taken once");
		Require(crash->GameName == "Game AccessViolation", "The crash has the wrong game name");
		Require(crash->ExitCode == 0xC0000005, "The crashed game's exit code isn't the access violation's: " + std::to_string(crash->ExitCode));
		Require(crash->Report.has_value(), "The crashed game's report didn't arrive");
		Require(crash->Report->ExceptionCode == 0xC0000005, "The report has the wrong exception code");
		Require(crash->Report->Description.find("access violation writing address 0x0000000000000000") != std::string::npos,
			"The report doesn't describe the access violation: " + crash->Report->Description);

		const bool bFoundFrame = std::any_of(crash->Report->Frames.begin(), crash->Report->Frames.end(),
			[](const Nyx::CrashStackFrame& frame)
			{
				return frame.Function.find("CrashOnPurpose") != std::string::npos && frame.File.ends_with("GameInstanceTests.cpp") &&
					frame.Line > 0;
			});
		Require(bFoundFrame, "The call stack lacks the crashing function with its file and line:\n" + Nyx::FormatCrashReport(*crash->Report));
		const Nyx::CrashReport report = *crash->Report;

		// Enough to start it again as it was
		Require(crash->LaunchOptions.WindowTitle == "Game AccessViolation" && crash->LaunchOptions.WindowSize == GameWindowSize{ 640, 400 } &&
					!crash->bConsoleWindow,
			"The crash doesn't keep how the game was started");

		// An exit code without a report, e.g. a crash before the game was linked
		crash = exitCode.TakeCrash();
		Require(crash && crash->ExitCode == 3 && !crash->Report.has_value(), "An exit code other than 0 should give a crash without report");

		// Quitting normally is no crash
		fine.Stop();
		UpdateUntil(games,
			[&]()
			{
				return !fine.IsRunning();
			},
			"the fine game quit");
		Require(!fine.TakeCrash().has_value(), "A game that quit normally left a crash");

		// The message describes itself, for the Messages tab and NyxDump
		const EditorLinkMessageText text = DescribeEditorLinkMessage(MakeNetMessage(CrashMessage{ report }), true);
		Require(text.Name == "Crash" && text.Details.find("CrashOnPurpose") != std::string::npos,
			"A Crash message doesn't describe its call stack");
	}

	void TestStoppingBeforeEditorCloses()
	{
		const std::filesystem::path executable = Nyx::Paths::GetExecutableDir() / "NyxGameInstanceTests.exe";
		LogLines lines;

		std::vector<std::unique_ptr<GameInstance>> games;
		for (const char* name : { "Game 1", "Game 2" })
		{
			GameLaunchOptions options;
			options.WindowTitle = name;
			games.push_back(std::make_unique<GameInstance>(name, lines.MakeObserver()));
			Require(games.back()->Start(executable, options, false), std::string("Couldn't start ") + name);
		}

		UpdateUntil(games,
			[&]()
			{
				return IsLinked(*games[0]) && IsLinked(*games[1]);
			},
			"both games are linked");

		// As the editor does when it closes: ask all, then give each the rest of the time
		const auto deadline = std::chrono::steady_clock::now() + GameInstance::QuitTimeLimit;
		for (const std::unique_ptr<GameInstance>& game : games)
		{
			game->AskToQuitBeforeEditorCloses();
		}
		for (const std::unique_ptr<GameInstance>& game : games)
		{
			game->FinishBeforeEditorCloses(deadline);
			Require(!game->IsRunning(), game->GetName() + " still runs after the editor closed");
		}

		Require(std::chrono::steady_clock::now() < deadline, "The games should have quit before the deadline");
	}
}

int main(int argc, char** argv)
{
	const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);

	// Started by the tests below as a game
	Nyx::Core::Logger::Get().Init();

	if (std::find(arguments.begin(), arguments.end(), "--editor-port") != arguments.end())
	{
		return RunStandInGame(arguments);
	}

	try
	{
		TestUnstartedGame();
		TestSeveralGames();
		TestCrashReports();
		TestStoppingBeforeEditorCloses();

		std::cout << "All game instance tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game instance test failed: " << error.what() << "\n";
		return 1;
	}
}

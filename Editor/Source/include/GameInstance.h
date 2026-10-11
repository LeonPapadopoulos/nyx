#pragma once

#include "ChildProcess.h"
#include "CrashHandler.h"
#include "EditorLink.h"
#include "GameLaunchOptions.h"
#include "NetConnection.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Nyx::Editor
{
	// A game that ended by itself with an exit code other than 0, for the editor's crash card
	struct GameCrash
	{
		std::string GameName;
		uint32_t ExitCode = 0;

		// What the game's crash handler sent before the game ended. None if it couldn't, e.g.
		// because it wasn't linked yet, was ended from outside, or exited with an error code.
		std::optional<Nyx::CrashReport> Report;

		// To start it again as it was started (Restart)
		Nyx::Engine::GameLaunchOptions LaunchOptions;
		bool bConsoleWindow = true;
	};

	// One game the editor started with Play: its process, and the editor link to it. Play starts
	// one, Play All several; each has its own listener port, link and queue of edits, so one game
	// that crashes, is closed or sits in the debugger doesn't hold up the others.
	class GameInstance
	{
	public:
		// Sees every message of this game's link, both ways, also of programs that connect to its
		// port and turn out not to be the game. For the Game Link window.
		using MessageObserver =
			std::function<void(const std::string& gameName, Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message)>;

		// name tells this game apart in the log and the Game Link window, e.g. "Game 2"
		GameInstance(std::string name, MessageObserver observer);

		// Ends the game if it still runs, so no game outlives the editor's record of it
		~GameInstance();

		GameInstance(const GameInstance&) = delete;
		GameInstance& operator=(const GameInstance&) = delete;

		// Listens for the game's connection, then starts the game with the options and the
		// listener's port. Without a port, the game runs unlinked. The game's console window shows
		// its log, which also reaches the Game Link window once linked. Returns whether it started.
		bool Start(const std::filesystem::path& gameExecutable, Nyx::Engine::GameLaunchOptions options, bool bConsoleWindow = true);

		// Once per frame: accepts the game's connection, handles its messages, sends queued edits,
		// and notices when the game exited or didn't quit in time.
		void Update();

		// Edits for this game, sent in order once it is linked. Ignored once it doesn't take edits.
		void QueueEdits(const std::vector<Nyx::Net::Message>& edits);

		// From Start() until the game was asked to quit, ended, or its link ended. A game that
		// falls more than MaxQueuedEditBytes behind stops taking edits too.
		bool TakesEdits() const
		{
			return bTakesEdits;
		}

		// Asks the game to quit, and ends it if it doesn't within QuitTimeLimit. Ends it at once if
		// it isn't linked, or was asked already.
		void Stop();

		// Ends the game at once, without letting it clean up
		void EndNow();

		// Closing the editor, in two steps over all games so they quit at the same time: first
		// every game is asked to quit, then each gets until the deadline to do it.
		void AskToQuitBeforeEditorCloses();
		void FinishBeforeEditorCloses(std::chrono::steady_clock::time_point deadline);

		bool IsRunning() const
		{
			return bRunning;
		}

		// Asked to quit, and not exited yet
		bool IsQuitting() const
		{
			return bRunning && QuitDeadline.has_value();
		}

		const std::string& GetName() const
		{
			return Name;
		}

		uint32_t GetProcessId() const
		{
			return Process.GetProcessId();
		}

		// For the Stop button's tooltip, e.g. "connected to NyxGame (process 1234)"
		std::string GetLinkStatus() const;

		// Once the game has ended by itself with an exit code other than 0: why, once
		std::optional<GameCrash> TakeCrash()
		{
			return std::exchange(Crash, std::nullopt);
		}

		static constexpr std::chrono::seconds QuitTimeLimit{ 3 };

		// A game paused in the debugger doesn't read. Edits wait for it up to this much, then it
		// gets no more until the next Play.
		static constexpr size_t MaxQueuedEditBytes = 16 * 1024 * 1024;

	private:
		void UpdateLink();
		void HandleMessages();
		void SendQueuedEdits();
		void CheckWhetherExited();
		void StopTakingEdits();

	private:
		std::string Name;
		MessageObserver Observer;

		Nyx::ChildProcess Process;

		// Whether the game was running at the last check, to notice when it exits by itself
		bool bRunning = false;

		// The game connects back on this port, passed to it as --editor-port
		Nyx::Net::Listener Listener;

		// The link to the game, once it said Hello
		std::unique_ptr<Nyx::Engine::EditorLink> Link;

		// Connections that haven't said Hello yet. The first whose Hello comes from the game's
		// process becomes Link, so a program that connects and stays silent can't keep the game out.
		std::vector<std::unique_ptr<Nyx::Engine::EditorLink>> Candidates;

		// Why the last link ended, for the tooltip
		std::string CloseReason;

		// Set while the game was asked to quit: when it gets ended instead
		std::optional<std::chrono::steady_clock::time_point> QuitDeadline;

		// As passed to Start(), for a restart after a crash
		Nyx::Engine::GameLaunchOptions LaunchOptions;
		bool bConsoleWindow = true;

		// From the game's Crash message, until it has exited
		std::optional<Nyx::CrashReport> CrashReport;
		std::optional<GameCrash> Crash;

		bool bTakesEdits = false;
		std::vector<Nyx::Net::Message> QueuedEdits;
		size_t QueuedEditBytes = 0;
	};
}

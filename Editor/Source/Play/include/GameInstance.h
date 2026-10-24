#pragma once

#include "ChildProcess.h"
#include "CrashHandler.h"
#include "EditorLink.h"
#include "GameLaunchOptions.h"
#include "InputRecording.h"
#include "InputStream.h"
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
		Nyx::GameLaunchOptions LaunchOptions;
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
			std::function<void(const std::string& gameName, Nyx::ELinkDirection direction, const Nyx::Net::Message& message)>;

		// name tells this game apart in the log and the Game Link window, e.g. "Game 2"
		GameInstance(std::string name, MessageObserver observer);

		// Ends the game if it still runs, so no game outlives the editor's record of it
		~GameInstance();

		GameInstance(const GameInstance&) = delete;
		GameInstance& operator=(const GameInstance&) = delete;

		// Listens for the game's connection, then starts the game with the options and the
		// listener's port. Without a port, the game runs unlinked. The game's console window shows
		// its log, which also reaches the Game Link window once linked. Returns whether it started.
		bool Start(const std::filesystem::path& gameExecutable, Nyx::GameLaunchOptions options, bool bConsoleWindow = true);

		// Once per frame: accepts the game's connection, handles its messages, sends queued edits,
		// and notices when the game exited or didn't quit in time.
		void Update();

		// Edits for this game, sent in order once it is linked. Ignored once it doesn't take edits.
		void QueueEdits(const std::vector<Nyx::Net::Message>& edits);

		// Whether the game streams its input frames to a replica here, for the Input window. The
		// game is asked as soon as it is linked; turning it on again starts a new replica.
		void SetInputSubscribed(bool bSubscribed);

		// The game's input as the replica decides it; null while not subscribed
		Nyx::Input::InputReplica* GetInputReplica()
		{
			return InputReplica.get();
		}

		// Asks the game for the entity's run-time components every InspectInterval while it is linked,
		// and only once the last answer came, so a game that doesn't read isn't sent a pile of
		// questions. Another entity drops the answer so far; none stops asking.
		void SetInspectedEntity(std::optional<Nyx::EntityGuid> entity);

		// The game's last answer about the entity asked about now; null until one came
		const Nyx::InspectedEntityMessage* GetInspection() const
		{
			return Inspection ? &*Inspection : nullptr;
		}

		static constexpr std::chrono::milliseconds InspectInterval{ 250 };

		// Records the game's input to the file: subscribes, and has the game start its stream
		// again, so the file starts with a snapshot. False if the file can't be written.
		bool StartInputRecording(const std::filesystem::path& path);
		void StopInputRecording();

		bool IsRecordingInput() const
		{
			return InputRecorder.IsOpen();
		}

		const Nyx::Input::InputRecorder& GetInputRecorder() const
		{
			return InputRecorder;
		}

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
		void HandleInputRecord(const Nyx::Net::Message& message);
		void HandleInspection(const Nyx::Net::Message& message);
		void AskForInspection();
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
		std::unique_ptr<Nyx::EditorLink> Link;

		// Connections that haven't said Hello yet. The first whose Hello comes from the game's
		// process becomes Link, so a program that connects and stays silent can't keep the game out.
		std::vector<std::unique_ptr<Nyx::EditorLink>> Candidates;

		// Why the last link ended, for the tooltip
		std::string CloseReason;

		// Set while the game was asked to quit: when it gets ended instead
		std::optional<std::chrono::steady_clock::time_point> QuitDeadline;

		// As passed to Start(), for a restart after a crash
		Nyx::GameLaunchOptions LaunchOptions;
		bool bConsoleWindow = true;

		// From the game's Crash message, until it has exited
		std::optional<Nyx::CrashReport> CrashReport;
		std::optional<GameCrash> Crash;

		bool bTakesEdits = false;
		std::vector<Nyx::Net::Message> QueuedEdits;
		size_t QueuedEditBytes = 0;

		// The game's input, while subscribed, and what the game was last told
		bool bInputSubscribed = false;
		bool bInputSubscriptionSent = false;
		std::unique_ptr<Nyx::Input::InputReplica> InputReplica;
		bool bInputRecordWarned = false;

		// The entity asked about, the game's last answer about it, the last question's number, and
		// whether its answer is still to come
		std::optional<Nyx::EntityGuid> InspectedEntity;
		std::optional<Nyx::InspectedEntityMessage> Inspection;
		uint32_t InspectRequest = 0;
		bool bInspectAnswerPending = false;
		std::chrono::steady_clock::time_point LastInspectSent;

		// Recording: the stream asked to start again, and records of the old one skipped until the
		// new one's snapshot (its definitions) comes
		Nyx::Input::InputRecorder InputRecorder;
		bool bRestartInputStream = false;
		bool bAwaitingInputSnapshot = false;
	};
}

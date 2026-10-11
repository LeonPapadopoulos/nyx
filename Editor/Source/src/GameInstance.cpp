#include "GameInstance.h"

#include "EditorLinkMessages.h"
#include "Log.h"

#include <algorithm>
#include <utility>

namespace Nyx::Editor
{
	GameInstance::GameInstance(std::string name, MessageObserver observer)
		: Name(std::move(name))
		, Observer(std::move(observer))
	{
	}

	GameInstance::~GameInstance()
	{
		EndNow();
	}

	bool GameInstance::Start(const std::filesystem::path& gameExecutable, Nyx::Engine::GameLaunchOptions options, bool bConsoleWindow)
	{
		// The game connects back to the editor on this port: the editor link. Only programs on
		// this machine can connect. Without it, the game still runs, just not linked.
		if (!Listener.IsListening() && !Listener.Listen())
		{
			LOG_WARNING("{0}: the editor can't wait for the game to connect ({1}); it runs without the editor link", Name,
				Listener.GetError());
		}

		options.EditorPort = Listener.IsListening() ? std::optional<uint16_t>(Listener.GetPort()) : std::nullopt;

		CloseReason.clear();
		QuitDeadline.reset();
		CrashReport.reset();
		Crash.reset();
		LaunchOptions = options;
		this->bConsoleWindow = bConsoleWindow;

		if (!Process.Start(gameExecutable, Nyx::Engine::MakeGameArguments(options), bConsoleWindow))
		{
			LOG_ERROR("{0}: couldn't start '{1}'", Name, gameExecutable.string());
			Listener.Close();
			return false;
		}

		bRunning = true;

		// The game runs the scene as saved before Play. Edits from now on are sent once it is
		// linked; without the link, nothing could take them.
		bTakesEdits = options.EditorPort.has_value();
		QueuedEdits.clear();
		QueuedEditBytes = 0;

		if (options.bWaitForDebugger)
		{
			LOG_INFO("{0}: started (process {1}); it waits until a debugger is attached", Name, Process.GetProcessId());
		}
		else
		{
			LOG_INFO("{0}: started (process {1})", Name, Process.GetProcessId());
		}

		return true;
	}

	void GameInstance::Update()
	{
		UpdateLink();
		CheckWhetherExited();

		if (QuitDeadline && std::chrono::steady_clock::now() >= *QuitDeadline)
		{
			LOG_WARNING("{0} didn't quit within {1} seconds; ending it", Name, QuitTimeLimit.count());
			EndNow();
		}
	}

	void GameInstance::QueueEdits(const std::vector<Nyx::Net::Message>& edits)
	{
		if (!bTakesEdits)
		{
			return;
		}

		for (const Nyx::Net::Message& edit : edits)
		{
			QueuedEditBytes += edit.Payload.size();
			QueuedEdits.push_back(edit);
		}

		if (QueuedEditBytes > MaxQueuedEditBytes)
		{
			LOG_WARNING("{0}: {1} MB of edits are waiting for a game that doesn't take them; it won't get edits until the next Play",
				Name, QueuedEditBytes / (1024 * 1024));
			StopTakingEdits();
		}
	}

	void GameInstance::Stop()
	{
		if (!Process.IsRunning() || QuitDeadline)
		{
			EndNow();
			return;
		}

		// Asked to quit, the game ends like when its window is closed: its last log lines arrive
		// and its files are closed. Update() ends it if it doesn't within the time limit.
		if (Link && Link->IsConnected())
		{
			// Edits from now on, e.g. to a scene opened meanwhile, aren't for this game
			StopTakingEdits();

			Link->Send(Nyx::Engine::QuitMessage{});
			QuitDeadline = std::chrono::steady_clock::now() + QuitTimeLimit;
			LOG_INFO("Asked {0} to quit", Name);
			return;
		}

		EndNow();
	}

	void GameInstance::EndNow()
	{
		StopTakingEdits();

		if (Link)
		{
			Link->Close("the editor ended the game");
			Link.reset();
		}
		Candidates.clear();
		Listener.Close();
		QuitDeadline.reset();

		const bool bWasRunning = Process.IsRunning();
		Process.Terminate();
		bRunning = false;

		if (bWasRunning)
		{
			LOG_INFO("Ended {0}", Name);
		}
	}

	void GameInstance::AskToQuitBeforeEditorCloses()
	{
		// The editor can't wait over several frames now. The game gets Quit (unless it has it
		// already) and the rest of its time to exit by itself.
		if (Process.IsRunning() && !QuitDeadline && Link && Link->IsConnected())
		{
			StopTakingEdits();
			Link->Send(Nyx::Engine::QuitMessage{});
			QuitDeadline = std::chrono::steady_clock::now() + QuitTimeLimit;
		}
	}

	void GameInstance::FinishBeforeEditorCloses(std::chrono::steady_clock::time_point deadline)
	{
		const bool bAskedToQuit = QuitDeadline.has_value();

		if (Process.IsRunning() && Link && Link->IsConnected())
		{
			// Waits until the game has read everything and closed its end, so Quit can't get lost
			Link->CloseGracefully("the editor is closing", std::chrono::milliseconds(1000));
			Link.reset();
		}

		if (Process.IsRunning() && bAskedToQuit)
		{
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
			Process.WaitForExit(static_cast<uint32_t>(std::max<long long>(remaining.count(), 0)));
		}

		EndNow();
	}

	std::string GameInstance::GetLinkStatus() const
	{
		if (Link)
		{
			const Nyx::Engine::HelloMessage& game = Link->GetOtherSide();
			return "connected to " + game.ProgramName + " (process " + std::to_string(game.ProcessId) + ")";
		}

		if (!Candidates.empty())
		{
			return "saying Hello";
		}

		if (!CloseReason.empty())
		{
			return "closed: " + CloseReason;
		}

		return Listener.IsListening() ? "waiting for the game to connect" : "off";
	}

	void GameInstance::UpdateLink()
	{
		// Only this game may connect to its port: its Hello has to come from its process
		while (std::unique_ptr<Nyx::Net::Connection> connection = Listener.Accept())
		{
			// Without a running game, this is usually a game that was stopped while it connected
			if (!bRunning)
			{
				LOG_INFO("{0}: editor link: closed a connection that came in while the game wasn't running", Name);
				continue;
			}

			if (Link)
			{
				LOG_WARNING("{0}: editor link: turned away another program that connected while the game is connected", Name);
				continue;
			}

			auto candidate = std::make_unique<Nyx::Engine::EditorLink>(std::move(connection), "NyxEditor",
				[this](Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message)
				{
					if (Observer)
					{
						Observer(Name, direction, message);
					}
				});
			candidate->RequireOtherProcessId(Process.GetProcessId());
			Candidates.push_back(std::move(candidate));
		}

		for (std::unique_ptr<Nyx::Engine::EditorLink>& candidate : Candidates)
		{
			candidate->Update();

			// Even if the game exited right after its Hello, its messages are handled below
			if (!Link && candidate->HadHandshake())
			{
				Link = std::move(candidate);
			}
			else if (candidate->GetState() == Nyx::Engine::EEditorLinkState::Closed)
			{
				CloseReason = candidate->GetCloseReason();
			}
		}

		// Failed candidates logged why. Once the game is linked, the others aren't needed.
		if (Link)
		{
			Candidates.clear();
		}
		else
		{
			std::erase_if(Candidates,
				[](const std::unique_ptr<Nyx::Engine::EditorLink>& candidate)
				{
					return !candidate || candidate->GetState() == Nyx::Engine::EEditorLinkState::Closed;
				});
		}

		if (!Link)
		{
			return;
		}

		Link->Update();
		HandleMessages();
		SendQueuedEdits();

		// The link logged why it ended. The game doesn't connect again, so edits can't reach it anymore.
		if (Link->GetState() == Nyx::Engine::EEditorLinkState::Closed)
		{
			CloseReason = Link->GetCloseReason();
			Link.reset();
			StopTakingEdits();
		}
	}

	void GameInstance::HandleMessages()
	{
		while (std::optional<Nyx::Net::Message> message = Link->Receive())
		{
			// The Game Link window shows log lines; it sees every message through the link's observer
			if (message->Type == static_cast<uint16_t>(Nyx::Engine::EEditorLinkMessage::LogLine))
			{
				continue;
			}

			// The game is about to end; the report goes on its crash card once it has
			if (message->Type == static_cast<uint16_t>(Nyx::Engine::EEditorLinkMessage::Crash))
			{
				Nyx::Engine::CrashMessage crash;
				if (Nyx::Engine::ReadNetMessage(*message, crash))
				{
					LOG_ERROR("{0} crashed: {1}", Name, crash.Report.Description);
					CrashReport = std::move(crash.Report);
				}
				else
				{
					LOG_WARNING("{0}: editor link: a crash report can't be read", Name);
				}
				continue;
			}

			LOG_WARNING("{0}: editor link: skipped a message of type {1}, which this editor doesn't know", Name, message->Type);
		}
	}

	void GameInstance::SendQueuedEdits()
	{
		// Edits made since the last frame, or while the game was starting. A game paused in the
		// debugger doesn't read; until it does, edits wait here, up to MaxQueuedEditBytes.
		constexpr size_t MaxUnsentBytes = 1024 * 1024;
		if (QueuedEdits.empty() || !Link->IsConnected() || Link->GetUnsentSize() > MaxUnsentBytes)
		{
			return;
		}

		for (const Nyx::Net::Message& edit : QueuedEdits)
		{
			Link->Send(edit);
		}

		QueuedEdits.clear();
		QueuedEditBytes = 0;
	}

	void GameInstance::CheckWhetherExited()
	{
		if (!bRunning || Process.IsRunning())
		{
			return;
		}

		// One more pass for the game's last messages, also from a game that connected just before
		// it exited, and for the link to notice that the game is gone
		UpdateLink();
		Link.reset();
		Candidates.clear();
		Listener.Close();
		QuitDeadline.reset();
		StopTakingEdits();

		bRunning = false;

		// Closing the game's window exits with 0; a crash exits with a code such as 0xC0000005
		// (access violation), which the editor survives.
		const uint32_t exitCode = Process.GetExitCode().value_or(0);
		if (exitCode == 0)
		{
			LOG_INFO("{0} exited", Name);
		}
		else
		{
			LOG_WARNING("{0} exited with code 0x{1:08X}", Name, exitCode);

			// Also without a report, e.g. a crash before the game was linked: it gets a card
			Crash = GameCrash{
				.GameName = Name,
				.ExitCode = exitCode,
				.Report = std::move(CrashReport),
				.LaunchOptions = LaunchOptions,
				.bConsoleWindow = bConsoleWindow };
			CrashReport.reset();
		}
	}

	void GameInstance::StopTakingEdits()
	{
		bTakesEdits = false;
		QueuedEdits.clear();
		QueuedEditBytes = 0;
	}
}

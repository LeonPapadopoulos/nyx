#include "PlaySession.h"

#include "EditorPreferences.h"
#include "GameLinkPanel.h"
#include "Log.h"
#include "Paths.h"
#include "SceneDocument.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Nyx::Editor
{
	PlaySession::PlaySession(const Nyx::SceneDocument& scene, GameLinkPanel& gameLinkWindow)
		: Scene(scene)
		, GameLinkWindow(gameLinkWindow)
		, GameEdits(scene)
	{
	}

	void PlaySession::Start(const EditorPreferences& preferences, bool bPlayAll, const PlayWallScreen& screen)
	{
		const GamePlan plan = PlanGames(preferences, bPlayAll, screen);
		if (plan.Games.empty())
		{
			LOG_WARNING("Play All: no play setup is marked for Play All (Play's right-click menu, or Window > Play Setups)");
			return;
		}

		if (!plan.bFits)
		{
			LOG_WARNING("Play All: {0} games don't fit on this monitor even at {1}%; some windows go past its bottom edge",
				plan.Games.size(), static_cast<int>(MinPlayWallScale * 100.0f));
		}
		else if (plan.Scale < 1.0f)
		{
			LOG_INFO("Play All: the games are shown at {0}% of their setups' sizes, to fit on this monitor",
				static_cast<int>(plan.Scale * 100.0f));
		}

		const std::optional<std::filesystem::path> playSessionScene = SavePlaySessionScene();
		if (!playSessionScene)
		{
			return;
		}

		GameLinkWindow.OnPlayStarted();

		for (const PlannedGame& planned : plan.Games)
		{
			Nyx::Engine::GameLaunchOptions launchOptions = planned.Options;
			launchOptions.ScenePath = *playSessionScene;
			launchOptions.bWaitForDebugger = bGameWaitsForDebugger;
			LaunchGame(planned.Name, launchOptions, planned.bConsoleWindow);
		}

		// Edits from now on are kept for the games that can take them, and sent once each is linked
		if (AnyTakesEdits())
		{
			GameEdits.StartSession();
		}
	}

	std::optional<std::filesystem::path> PlaySession::SavePlaySessionScene()
	{
		// The games run a copy of the open scene, saved next to the executables. That includes
		// unsaved changes and leaves the scene's own file alone. Each game reads it as it starts.
		const std::filesystem::path playSessionScene = Nyx::Paths::GetExecutableDir() / "PlaySession.nyxscene";
		if (!Nyx::Engine::SceneSerializer::SaveToFile(Scene.GetRegistry(), playSessionScene))
		{
			LOG_ERROR("Couldn't save the scene for the game to '{0}'", playSessionScene.string());
			return std::nullopt;
		}

		return playSessionScene;
	}

	bool PlaySession::LaunchGame(const std::string& name, const Nyx::Engine::GameLaunchOptions& launchOptions, bool bConsoleWindow)
	{
		// NyxGame is built next to the editor, since building the editor builds it too
		const std::filesystem::path gameExecutable = Nyx::Paths::GetExecutableDir() / "NyxGame.exe";

		auto game = std::make_unique<GameInstance>(name,
			[this](const std::string& gameName, Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message)
			{
				GameLinkWindow.AddMessage(gameName, direction, message);
			});

		if (!game->Start(gameExecutable, launchOptions, bConsoleWindow))
		{
			return false;
		}
		// What the files don't have yet, before the edits to come
		if (StateForNewGames)
		{
			game->QueueEdits(StateForNewGames());
		}

		Games.push_back(std::move(game));
		return true;
	}

	void PlaySession::Restart(const GameCrash& crash)
	{
		// The scene as it is now, like Play: edits made since the crash are included
		const std::optional<std::filesystem::path> playSessionScene = SavePlaySessionScene();
		if (!playSessionScene)
		{
			return;
		}

		Nyx::Engine::GameLaunchOptions launchOptions = crash.LaunchOptions;
		launchOptions.ScenePath = *playSessionScene;

		if (!LaunchGame(crash.GameName, launchOptions, crash.bConsoleWindow))
		{
			return;
		}

		// Other games may still be running and getting edits; their session goes on. Starting a
		// new one would drop edits they haven't been given yet.
		if (!GameEdits.IsSessionActive() && Games.back()->TakesEdits())
		{
			GameEdits.StartSession();
		}

		LOG_INFO("Restarted {0}", crash.GameName);
	}

	void PlaySession::Stop()
	{
		// Stop pressed again while games are quitting: ends them all at once
		if (IsQuitting())
		{
			EndNow();
			return;
		}

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->Stop();
		}

		// Edits from now on, e.g. to a scene opened meanwhile, aren't for these games
		GameEdits.EndSession();
	}

	void PlaySession::EndNow()
	{
		GameEdits.EndSession();

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->EndNow();
		}

		Games.clear();
	}

	void PlaySession::StopBeforeEditorCloses()
	{
		// All games are asked first, so they quit at the same time, and share one deadline
		const auto deadline = std::chrono::steady_clock::now() + GameInstance::QuitTimeLimit;

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->AskToQuitBeforeEditorCloses();
		}

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->FinishBeforeEditorCloses(deadline);
		}

		EndNow();
	}

	void PlaySession::OnSceneReplaced()
	{
		// A game that is quitting already gets the rest of its time.
		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			if (!game->IsQuitting())
			{
				game->Stop();
			}
		}
		GameEdits.EndSession();
	}

	void PlaySession::Update()
	{
		// Each game gets every edit, in its own queue: one that sits in the debugger or crashed
		// only stops itself from getting edits
		const std::vector<Nyx::Net::Message> edits = GameEdits.TakeMessages();

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->QueueEdits(edits);
			game->Update();
		}

		// A game that ended by itself with an error is kept for its crash card
		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			if (std::optional<GameCrash> crash = game->TakeCrash())
			{
				Crashes.push_back(std::move(*crash));
			}
		}

		// A game that exited, or was ended, logged it; it doesn't come back
		std::erase_if(Games,
			[](const std::unique_ptr<GameInstance>& game)
			{
				return !game->IsRunning();
			});

		if (!AnyTakesEdits() && GameEdits.IsSessionActive())
		{
			GameEdits.EndSession();
		}
	}

	std::vector<GameCrash> PlaySession::TakeCrashes()
	{
		return std::exchange(Crashes, {});
	}

	bool PlaySession::IsQuitting() const
	{
		return std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->IsQuitting();
			});
	}

	bool PlaySession::AnyTakesEdits() const
	{
		return std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->TakesEdits();
			});
	}

	std::string PlaySession::GetLinkStatus() const
	{
		std::string status;

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			status += "\n" + game->GetName() + ": " + game->GetLinkStatus();
		}

		return status;
	}
}

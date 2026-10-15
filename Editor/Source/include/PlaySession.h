#pragma once

#include "GameInstance.h"
#include "GameLinkSubscriber.h"
#include "PlayWall.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	class GameLinkPanel;
	struct EditorPreferences;

	// Play and Stop: the open scene runs in NyxGame, as separate programs. Play starts one game
	// with the first play setup; Play All one per setup marked for it, tiled on the editor's
	// monitor. Stop asks each game to quit, and ends those that don't within GameInstance::QuitTimeLimit.
	// Every edit, undo and redo reaches the running games (GameLinkSubscriber).
	class PlaySession
	{
	public:
		// The scene is what the games run; the Game Link window sees their logs and messages.
		PlaySession(const Nyx::SceneDocument& scene, GameLinkPanel& gameLinkWindow);

		PlaySession(const PlaySession&) = delete;
		PlaySession& operator=(const PlaySession&) = delete;

		// For TransactionSystem::Subscribe: turns edits into messages for the games
		GameLinkSubscriber& GetEditSubscriber()
		{
			return GameEdits;
		}

		// A message for every game that takes edits, e.g. a shader the editor compiled or a
		// material's values; dropped while none is running
		void SendToGames(Nyx::Net::Message message)
		{
			GameEdits.Queue(std::move(message));
		}

		// What a game gets first, before any edit, when it starts: what the editor has that the
		// files don't, e.g. a material's unsaved values
		std::function<std::vector<Nyx::Net::Message>()> StateForNewGames;

		// screen is the monitor Play All tiles the games on
		void Start(const EditorPreferences& preferences, bool bPlayAll, const PlayWallScreen& screen);

		// Asks the games to quit. Pressed again while they are quitting, ends them at once.
		void Stop();
		void EndNow();

		// When the editor closes: asks all games at once, and waits for them up to one shared deadline
		void StopBeforeEditorCloses();

		// The games keep running the previous scene when another is opened; they are stopped
		// (those quitting already get the rest of their time) and get no more edits.
		void OnSceneReplaced();

		// Restart on a crash card: the game again, as it was started, with the scene as it is now
		void Restart(const GameCrash& crash);

		// Each frame: hands the edits since the last frame to every game, updates the games and
		// forgets those that exited
		void Update();

		// The games that crashed since the last call, in the order they were noticed
		std::vector<GameCrash> TakeCrashes();

		bool IsRunning() const
		{
			return !Games.empty();
		}

		// Whether a game was asked to quit and hasn't yet
		bool IsQuitting() const;

		// For the Stop button's tooltip: one line per game, e.g. "Steam Deck: connected to NyxGame (process 1234)"
		std::string GetLinkStatus() const;

		// Set in Play's right-click menu: the games wait at startup until a debugger is attached
		bool bGameWaitsForDebugger = false;

	private:
		// Saves the open scene for games to run; without a path if that failed (logged)
		std::optional<std::filesystem::path> SavePlaySessionScene();

		// Starts one game and adds it to Games. Returns whether it started.
		bool LaunchGame(const std::string& name, const Nyx::Engine::GameLaunchOptions& launchOptions, bool bConsoleWindow);

		bool AnyTakesEdits() const;

	private:
		const Nyx::SceneDocument& Scene;
		GameLinkPanel& GameLinkWindow;

		// The games started with Play that haven't exited yet, each with its own editor link
		std::vector<std::unique_ptr<GameInstance>> Games;

		std::vector<GameCrash> Crashes;

		// Turns edits, undo and redo into messages for the games, from Play on. Update() copies
		// them into each game's queue.
		GameLinkSubscriber GameEdits;
	};
}

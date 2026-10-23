#pragma once

#include <memory>

namespace Nyx
{
	class GameWorld;
	class IPlayerController;

	namespace Input
	{
		class InputSystem;
	}

	// What a game's module brings to the programs that run it, the editor and the game: its own
	// reflected component types, registered next to the engine's, its simulation systems and its
	// player. The engine never knows a game;
	// a program hands its module to the Application (ApplicationSpecs::Module).
	struct GameModule
	{
		// Shown in the log; empty for a program built without a game module
		const char* Name = "";

		// Registers the module's component types and their post-load steps. nullptr: none.
		void (*RegisterTypes)() = nullptr;

		// The game only: adds the module's simulation systems to the world, in their order. The
		// editor doesn't simulate, so it never calls this. nullptr: none.
		void (*CreateSystems)(GameWorld& world) = nullptr;

		// The editor only: registers where the module's types are declared, so the editor can open
		// their source. nullptr: none.
		void (*RegisterSources)() = nullptr;

		// The game only: makes the player's controller, which turns input into commands each frame
		// (PlayerController.h), adding its actions and contexts to `input`. nullptr: the game has
		// no player of the module's.
		std::unique_ptr<IPlayerController> (*CreatePlayerController)(Input::InputSystem& input) = nullptr;
	};

	// The module the program was built with, defined by that module's library, or an empty one
	// (NyxNoGameModule) without. Only the programs call this; engine code gets the module from
	// its Application.
	const GameModule& GetLinkedGameModule();
}

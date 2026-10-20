#pragma once

namespace Nyx
{
	// What a game's module brings to the programs that run it, the editor and the game: its own
	// reflected component types, registered next to the engine's. The engine never knows a game;
	// a program hands its module to the Application (ApplicationSpecs::Module).
	struct GameModule
	{
		// Shown in the log; empty for a program built without a game module
		const char* Name = "";

		// Registers the module's component types and their post-load steps. nullptr: none.
		void (*RegisterTypes)() = nullptr;

		// The editor only: registers where the module's types are declared, so the editor can open
		// their source. nullptr: none.
		void (*RegisterSources)() = nullptr;
	};

	// The module the program was built with, defined by that module's library, or an empty one
	// (NyxNoGameModule) without. Only the programs call this; engine code gets the module from
	// its Application.
	const GameModule& GetLinkedGameModule();
}

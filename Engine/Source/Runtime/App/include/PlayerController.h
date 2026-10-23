#pragma once

#include "CameraProjection.h"

#include <optional>

namespace Nyx
{
	class CommandQueue;
	class GameWorld;

	namespace Input
	{
		class InputSystem;
	}

	// What a player controller gets each frame, before the frame's steps
	struct PlayerFrame
	{
		// The world as the last step left it, to look at: what the cursor is over, where the
		// player's character is. Not to change: what the player wants goes into Commands, and the
		// next step applies it (Docs/Design/simulation-loop.md).
		const GameWorld& World;
		// The actions the controller added (IPlayerController's maker gets the input system), as
		// this frame decided them; ConsumeTriggered() and the like go through its user
		Input::InputSystem& Input;
		// For the next step's systems (SimContext::Commands)
		CommandQueue& Commands;
		// The ray through the cursor while it is free and over the game's picture, from the scene's
		// primary camera; else empty
		std::optional<Ray> CursorRay;
		// The ray through the middle of the picture, e.g. to aim while the cursor is captured; empty
		// without a primary camera
		std::optional<Ray> ViewCenterRay;
		float DeltaSeconds = 0.0f;
	};

	// The game's player: turns input into commands once a frame. The game module makes it
	// (GameModule::CreatePlayerController), and it adds its actions and contexts to the input
	// system it is made with. It looks at the world and never changes it, so whatever reads the
	// commands sees the same thing whether they came from this player, a recording or, later,
	// another machine.
	class IPlayerController
	{
	public:
		virtual ~IPlayerController() = default;

		virtual void Update(const PlayerFrame& frame) = 0;
	};
}

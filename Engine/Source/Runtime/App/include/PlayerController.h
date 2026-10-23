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

	// What the player's UI is drawn over each frame: the game's picture, and the camera it shows
	struct PlayerView
	{
		const GameWorld& World;
		// The picture in ImGui's screen coordinates, where the UI draws
		glm::vec2 PictureMin{ 0.0f };
		glm::vec2 PictureSize{ 0.0f };
		// The scene's primary camera; empty without one
		std::optional<WorldCamera> Camera;

		// Where a point of the world shows, in ImGui's screen coordinates (WorldToScreenPoint());
		// empty behind the camera or without one
		std::optional<glm::vec2> ToScreen(const glm::vec3& point) const
		{
			if (!Camera)
			{
				return std::nullopt;
			}
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(Camera->Transform, Camera->Camera, PictureSize, point);
			return pixel ? std::optional<glm::vec2>(PictureMin + *pixel) : std::nullopt;
		}
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

		// Its own UI over the game's picture, with ImGui, inside the game's window: e.g. a hotbar,
		// or a ghost of what a click would place, drawn with ImGui::GetWindowDrawList() at
		// ToScreen() of world points. In every build, unlike the world's debug drawing; it changes
		// nothing in the world either.
		virtual void DrawUI(const PlayerView& /*view*/)
		{
		}
	};
}

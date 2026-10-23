#pragma once

#include "Entity.h"
#include "PlayerController.h"
#include "ThirdPersonInput.h"

#include <optional>

namespace Nyx
{
	// The player Nyx's game has when the game module brings none: while the world has a character and
	// an orbit camera, it walks the character (FindThirdPersonCharacter()) through MoveCharacterCommands,
	// W/A/S/D or the left stick relative to where the camera looks, Space or the pad's south button
	// to jump. It asks for the orbit camera's free look while exploring; Tab switches to holding the
	// right mouse button to look, with the cursor free, and back. Its input is ThirdPersonInput's.
	class ThirdPersonPlayer final : public IPlayerController
	{
	public:
		// The orbit camera's free look context is found by its default name
		// (OrbitCameraInput::Settings)
		explicit ThirdPersonPlayer(Input::InputSystem& input);

		void Update(const PlayerFrame& frame) override;

		bool IsFreeLook() const
		{
			return bFreeLook;
		}

	private:
		Input::ThirdPersonInput ThirdPerson;
		bool bFreeLook = true;
	};

	// The character a third person player walks: the one an orbit camera follows, else the first;
	// none without an orbit camera, or without a character
	std::optional<Entity> FindThirdPersonCharacter(const Registry& world);

	// MakeMoveCharacterCommand() (ThirdPersonInput.h) for a game's player, from its frame
	std::optional<MoveCharacterCommand> MakeMoveCharacterCommand(
		const PlayerFrame& frame, Entity character, const Input::ThirdPersonInput::Actions& actions);
}

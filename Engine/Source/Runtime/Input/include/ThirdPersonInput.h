#pragma once

#include "Entity.h"
#include "InputSystem.h"
#include "MoveCharacterCommand.h"

#include <optional>
#include <string>
#include <vector>

namespace Nyx::Input
{
	struct InputActionData;
	struct InputContextData;

	// The actions and context of a player who walks a character (CharacterComponent) seen from behind
	// (OrbitCameraComponent). One context, the third person context: Walk (W/A/S/D, left stick), Jump
	// (Space, the pad's south button) and ToggleFreeLook (Tab), for a player that lets the user switch
	// between looking around freely and pointing with the cursor (OrbitCameraInput's free look). The
	// player that uses it turns it on and off (SetActive()).
	class ThirdPersonInput
	{
	public:
		// The name its context is found by, and the defaults its bindings get when it adds them
		struct Settings
		{
			std::string ContextName = "ThirdPerson";
			int Priority = 100;
			// The stick's inner dead zone, of its travel
			float StickDeadZone = 0.15f;
		};

		struct Actions
		{
			ActionHandle Walk;           // Axis2D: x right, y forward, each up to 1
			ActionHandle Jump;           // Button, pressed
			ActionHandle ToggleFreeLook; // Button, pressed
		};

		// Uses the actions and context of its names in the input system, e.g. from an input asset;
		// adds them as MakeContextsData() has them if they aren't there. The input system must
		// outlive this.
		ThirdPersonInput(InputSystem& input, Settings settings);

		// Its actions and context as data, as the default input asset has them
		static std::vector<InputActionData> MakeActionsData();
		static std::vector<InputContextData> MakeContextsData(const Settings& settings);

		// Counts from the next frame, as all context changes do
		void SetActive(bool bActive, std::string reason);

		const Actions& GetActions() const
		{
			return PlayerActions;
		}

		ContextHandle GetContext() const
		{
			return Context;
		}

	private:
		InputSystem& Input;
		Settings PlayerSettings;
		Actions PlayerActions;
		ContextHandle Context;
	};

	// What the walk and the jump ask of the character this frame: the walk turned to the ground
	// relative to where the camera that follows the character looks (an orbit camera with it as its
	// target, else the scene's primary camera, else along the world's axes; GetGroundMove()), and a
	// jump if Jump fired (taken with ConsumeTriggered()). Empty for a character without a guid,
	// which a command can't name. A player sends one every frame, a still walk too, to stop it.
	std::optional<MoveCharacterCommand> MakeMoveCharacterCommand(
		const Registry& world, Entity character, InputUser& user, const ThirdPersonInput::Actions& actions);
}

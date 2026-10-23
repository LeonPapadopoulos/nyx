#pragma once

#include "Entity.h"
#include "FlyCameraInput.h"

namespace Nyx::Game
{
	// Flies the scene's primary camera, until the game has something better to do with its input:
	// W/A/S/D and the left stick move, E/Q and the triggers go up and down, the right stick turns,
	// the right mouse button held looks around with the mouse, the wheel changes the speed.
	// Without input it leaves the camera alone, so the editor's live edits still move it.
	class GameFlyCamera
	{
	public:
		// Adds the camera's actions and contexts to the input system, which must outlive it
		explicit GameFlyCamera(Nyx::Input::InputSystem& input);

		void Update(Nyx::Registry& world, float deltaTime);

	private:
		Nyx::Input::FlyCameraInput Fly;
		bool bLooking = false;

		// Metres per second; the wheel multiplies or divides it by 1.2 per notch
		float MoveSpeed = 5.0f;
	};
}

#pragma once

#include "InputSystem.h"

#include <glm/glm.hpp>

#include <string>

namespace Nyx::Input
{
	// The actions and contexts of a camera that flies freely, for the editor's scene views and the
	// game. Two contexts:
	// - the fly context: Move (W/A/S/D, left stick), MoveVertical (E/Q, right/left trigger), Look
	//   (right stick), Speed (mouse wheel) and LookButton (right mouse button);
	// - the mouse look context, above it, with the cursor captured: Look (the mouse).
	// The owner turns them on and off (SetActive), e.g. the fly context while a scene view is
	// hovered and the mouse look context while the right mouse button is held after a click into it.
	class FlyCameraInput
	{
	public:
		struct Settings
		{
			std::string FlyContextName = "FlyCamera";
			std::string MouseLookContextName = "FlyCameraMouseLook";
			int Priority = 100;

			// Radians per pixel the mouse moves
			float MouseSensitivity = 0.003f;
			// Radians per second at full stick; more after holding it there (look acceleration)
			float StickTurnRate = 2.5f;
			// The sticks' inner dead zone, of their travel
			float StickDeadZone = 0.15f;
		};

		struct Actions
		{
			ActionHandle Move;         // Axis3D: x right, z forward
			ActionHandle MoveVertical; // Axis1D: up
			ActionHandle Look;         // Axis2D: radians to turn this frame, x right, y up
			ActionHandle Speed;        // Axis1D: wheel notches, away from the user positive
			ActionHandle LookButton;   // Button
		};

		// What the camera should do this frame
		struct Values
		{
			// x right, y up, z forward; each part up to 1
			glm::vec3 Move{ 0.0f };
			// Radians to turn: x to the right, y up
			glm::vec2 Look{ 0.0f };
			float SpeedSteps = 0.0f;
			bool bLookButtonStarted = false;
			bool bLookButtonHeld = false;
		};

		// Adds the actions and contexts to the input system, which must outlive this
		FlyCameraInput(InputSystem& input, Settings settings);

		// Changes count from the next frame, as all context changes do. lookWindow: where the
		// cursor is captured while looking (null: the main window).
		void SetActive(bool bFlying, bool bLooking, std::string reason, void* lookWindow = nullptr);

		Values Read() const;

		const Actions& GetActions() const
		{
			return FlyActions;
		}

		ContextHandle GetFlyContext() const
		{
			return FlyContext;
		}

		ContextHandle GetMouseLookContext() const
		{
			return MouseLookContext;
		}

		const Settings& GetSettings() const
		{
			return FlySettings;
		}

	private:
		InputSystem& Input;
		Settings FlySettings;
		Actions FlyActions;
		ContextHandle FlyContext;
		ContextHandle MouseLookContext;
	};
}

#pragma once

#include "InputSystem.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Nyx
{
	class Registry;
}

namespace Nyx::Input
{
	struct InputActionData;
	struct InputContextData;

	// The actions and contexts of the game's orbit cameras (OrbitCameraComponent). Three contexts:
	// - the orbit context: OrbitLook (right stick), OrbitZoom (mouse wheel) and OrbitLookButton
	//   (right mouse button);
	// - the mouse look context, above it, with the cursor captured: OrbitLook (the mouse);
	// - the free look context, without bindings: a request. While someone, e.g. the game's player,
	//   keeps it on, the mouse looks around without a button, the cursor captured, as for exploring.
	//   While it is off, the mouse looks only while the right button is held, and the cursor is free
	//   to point with otherwise. The right stick looks either way.
	// UpdateOrbitCameras() turns the first two on and off; nothing here turns the request on.
	class OrbitCameraInput
	{
	public:
		// The names its contexts are found by, and the defaults its bindings get when it adds them
		struct Settings
		{
			std::string OrbitContextName = "OrbitCamera";
			std::string MouseLookContextName = "OrbitCameraMouseLook";
			std::string FreeLookContextName = "OrbitCameraFreeLook";
			int Priority = 100;

			// Radians per pixel the mouse moves
			float MouseSensitivity = 0.003f;
			// Radians per second at full stick; more after holding it there (look acceleration)
			float StickTurnRate = 2.5f;
			// The stick's inner dead zone, of its travel
			float StickDeadZone = 0.15f;
		};

		struct Actions
		{
			ActionHandle Look;       // Axis2D: radians to turn this frame, x right, y up
			ActionHandle Zoom;       // Axis1D: wheel notches, away from the user (in) positive
			ActionHandle LookButton; // Button
		};

		// What the cameras should do this frame
		struct Values
		{
			// Radians to turn: x to the right, y up
			glm::vec2 Look{ 0.0f };
			float ZoomSteps = 0.0f;
			bool bLookButtonStarted = false;
			bool bLookButtonHeld = false;
			// Whether the free look context is on
			bool bFreeLook = false;
		};

		// Uses the actions and contexts of its names in the input system, e.g. from an input asset;
		// adds them as MakeContextsData() has them if they aren't there. The input system must
		// outlive this.
		OrbitCameraInput(InputSystem& input, Settings settings);

		// Its actions and contexts as data, as the default input asset has them
		static std::vector<InputActionData> MakeActionsData();
		static std::vector<InputContextData> MakeContextsData(const Settings& settings);

		// Changes count from the next frame, as all context changes do. lookWindow: where the
		// cursor is captured while the mouse looks (null: the main window).
		void SetActive(bool bOrbiting, bool bMouseLooking, std::string reason, void* lookWindow = nullptr);

		Values Read() const;

		const Actions& GetActions() const
		{
			return OrbitActions;
		}

		ContextHandle GetOrbitContext() const
		{
			return OrbitContext;
		}

		ContextHandle GetMouseLookContext() const
		{
			return MouseLookContext;
		}

		// The request a player turns on for free look (InputUser::SetContextActive())
		ContextHandle GetFreeLookContext() const
		{
			return FreeLookContext;
		}

	private:
		InputSystem& Input;
		Settings OrbitSettings;
		Actions OrbitActions;
		ContextHandle OrbitContext;
		ContextHandle MouseLookContext;
		ContextHandle FreeLookContext;
	};

	// The game's orbit cameras' input, once per frame before the player's update: adds the look and
	// zoom of this frame to every OrbitCameraComponent's LookDelta and ZoomDelta, which
	// Nyx::UpdateOrbitCameras() (OrbitCameras.h) uses up after the frame's steps. The orbit context
	// is on only while the world has an entity with an OrbitCameraComponent and a TransformComponent,
	// the mouse look context while free look is asked for, or while the right mouse button is held
	// after a click. Writes only the components' input, never the simulation's state. Whether the
	// world has an orbit camera.
	bool UpdateOrbitCameras(Registry& world, OrbitCameraInput& input);
}

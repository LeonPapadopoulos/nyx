// The orbit camera's input (OrbitCameraInput, Input::UpdateOrbitCameras()) on a world without a
// window: its contexts are on only while the world has an orbit camera; the mouse turns it only
// while the right button is held, until free look is asked for, which turns it without a button and
// captures the cursor; the right stick turns it either way, and the wheel zooms. The default input
// asset has its actions and contexts.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "InputAsset.h"
#include "InputSystem.h"
#include "Log.h"
#include "OrbitCameraComponent.h"
#include "OrbitCameraInput.h"
#include "TestSupport.h"
#include "TransformComponent.h"
#include "VirtualInputBackend.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace Nyx::Input;
	using Nyx::Tests::Require;

	constexpr uint64_t Ms = 1000;

	// An input system with a virtual keyboard, mouse and pad, the orbit camera's input, and a world
	// with a camera entity
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Mouse;
		InputDeviceId Pad;
		uint64_t NowUs = 1000 * Ms;
		OrbitCameraInput Orbit{ Input, OrbitCameraInput::Settings{} };
		Nyx::Registry World;
		Nyx::Entity Camera;

		Rig()
		{
			Input.AddBackend(Backend);
			Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			Camera = World.CreateEntity();
			World.Add<Nyx::TransformComponent>(Camera, Nyx::TransformComponent{});
			World.Add<Nyx::CameraComponent>(Camera, Nyx::CameraComponent{});
		}

		// A frame 16 ms after the last, as the game runs it: the input, then the orbit cameras'.
		// Whether the world had one.
		bool Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			const bool bOrbiting = UpdateOrbitCameras(World, Orbit);
			Input.EndFrame();
			return bOrbiting;
		}

		void MoveMouse(const glm::vec2& pixels)
		{
			Backend.SetValue(Mouse, Nyx::Input::Mouse(EMouseControl::Delta), pixels, NowUs + 1 * Ms);
		}

		// What this frame's input added, then cleared, as the camera's update after the steps does
		glm::vec2 TakeLook()
		{
			Nyx::OrbitCameraComponent& orbit = World.Get<Nyx::OrbitCameraComponent>(Camera);
			return std::exchange(orbit.LookDelta, glm::vec2(0.0f));
		}

		bool IsOn(ContextHandle context) const
		{
			return Input.GetUser().IsContextActive(context);
		}
	};

	void TestTheContextsAreOnOnlyWithAnOrbitCamera()
	{
		Rig rig;
		Require(!rig.Frame(), "Without an OrbitCameraComponent there is no orbit camera");
		rig.Frame();
		Require(!rig.IsOn(rig.Orbit.GetOrbitContext()), "Without an OrbitCameraComponent the orbit context is off");

		rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
		Require(rig.Frame(), "With one, there is");
		rig.Frame();
		Require(rig.IsOn(rig.Orbit.GetOrbitContext()), "With an OrbitCameraComponent the orbit context is on");
		Require(!rig.IsOn(rig.Orbit.GetMouseLookContext()), "Without free look or the button, the mouse doesn't look");

		rig.World.Remove<Nyx::OrbitCameraComponent>(rig.Camera);
		rig.Frame();
		rig.Frame();
		Require(!rig.IsOn(rig.Orbit.GetOrbitContext()), "The orbit context goes off with the last OrbitCameraComponent");
	}

	void TestHoldToLook()
	{
		Rig rig;
		rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
		rig.Frame();
		rig.Frame();

		rig.MoveMouse(glm::vec2(100.0f, 0.0f));
		rig.Frame();
		Require(rig.TakeLook() == glm::vec2(0.0f), "The mouse alone doesn't turn the camera: the cursor is free to point");
		Require(rig.Input.GetUser().GetCursor().Mode == ECursorMode::Free, "The cursor is free");

		// From the frame after the click
		rig.Backend.Press(rig.Mouse, Mouse(EMouseControl::Right), rig.NowUs + 1 * Ms);
		rig.Frame();
		rig.Frame();
		Require(rig.World.Get<Nyx::OrbitCameraComponent>(rig.Camera).bMouseLooking && rig.IsOn(rig.Orbit.GetMouseLookContext()),
			"Holding the right mouse button looks around");
		rig.MoveMouse(glm::vec2(100.0f, -50.0f));
		rig.Frame();
		const glm::vec2 look = rig.TakeLook();
		Require(std::abs(look.x - 0.3f) < 1e-4f && std::abs(look.y - 0.15f) < 1e-4f,
			"Moving the mouse while it is held turns the camera: right and, with the mouse up, up");

		rig.Backend.Release(rig.Mouse, Mouse(EMouseControl::Right), rig.NowUs + 1 * Ms);
		rig.Frame();
		rig.Frame();
		rig.TakeLook();
		rig.MoveMouse(glm::vec2(100.0f, 0.0f));
		rig.Frame();
		Require(rig.TakeLook() == glm::vec2(0.0f) && !rig.IsOn(rig.Orbit.GetMouseLookContext()), "Let go, the mouse no longer turns it");
	}

	void TestFreeLookIsAskedForWithAContext()
	{
		Rig rig;
		rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
		rig.Frame();

		// As a game's player switches it, without touching the world
		rig.Input.GetUser().SetContextActive(rig.Orbit.GetFreeLookContext(), true, { .Reason = "Exploring" });
		rig.Frame();
		rig.Frame();
		Require(rig.IsOn(rig.Orbit.GetMouseLookContext()), "Free look turns the mouse look context on");
		Require(rig.Input.GetUser().GetCursor().Mode == ECursorMode::Captured, "Free look captures the cursor");
		rig.TakeLook();
		rig.MoveMouse(glm::vec2(100.0f, 0.0f));
		rig.Frame();
		Require(rig.TakeLook().x > 0.0f, "Free look turns with the mouse, no button held");

		rig.Input.GetUser().SetContextActive(rig.Orbit.GetFreeLookContext(), false);
		rig.Frame();
		rig.Frame();
		rig.TakeLook();
		Require(rig.Input.GetUser().GetCursor().Mode == ECursorMode::Free, "Back to holding the button, the cursor is free");
		rig.MoveMouse(glm::vec2(100.0f, 0.0f));
		rig.Frame();
		Require(rig.TakeLook() == glm::vec2(0.0f), "And the mouse alone no longer turns it");
	}

	void TestTheStickLooksAndTheWheelZooms()
	{
		for (const bool bFreeLook : { false, true })
		{
			Rig rig;
			rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
			rig.Input.GetUser().SetContextActive(rig.Orbit.GetFreeLookContext(), bFreeLook);
			rig.Frame();
			rig.Frame();
			rig.TakeLook();

			rig.Backend.SetValue(rig.Pad, Gamepad(EGamepadControl::RightStick), glm::vec3(1.0f, 0.0f, 0.0f), rig.NowUs + 1 * Ms);
			rig.Frame();
			Require(rig.TakeLook().x > 0.0f, std::string("The right stick turns the camera ") + (bFreeLook ? "in free look" : "while holding to look"));
		}

		Rig rig;
		rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
		rig.Frame();
		rig.Frame();
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Wheel), glm::vec2(0.0f, 2.0f), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.World.Get<Nyx::OrbitCameraComponent>(rig.Camera).ZoomDelta == 2.0f, "Two notches of the wheel away zoom in two steps");
	}

	// Whether one of the actions or contexts has the name
	template<typename T>
	bool HasNamed(const std::vector<T>& list, const std::string& name)
	{
		for (const T& item : list)
		{
			if (item.Name == name)
			{
				return true;
			}
		}
		return false;
	}

	void TestTheDefaultAssetHasThem()
	{
		const InputAssetData asset = MakeDefaultInputAsset();
		for (const char* action : { "OrbitLook", "OrbitZoom", "OrbitLookButton" })
		{
			Require(HasNamed(asset.Actions, action), std::string("The default asset has ") + action);
		}
		for (const char* context : { "OrbitCamera", "OrbitCameraMouseLook", "OrbitCameraFreeLook" })
		{
			Require(HasNamed(asset.Contexts, context), std::string("The default asset has ") + context);
		}

		// From an asset, its bindings are the asset's: a tuned sensitivity counts
		InputAssetData tuned = asset;
		for (InputContextData& context : tuned.Contexts)
		{
			if (context.Name == "OrbitCameraMouseLook")
			{
				context.Bindings[0].Pipeline.Sensitivity.Scale = glm::vec3(0.01f, 0.01f, 0.01f);
			}
		}
		Rig rig;
		ApplyInputAsset(rig.Input, tuned);
		rig.World.Add<Nyx::OrbitCameraComponent>(rig.Camera, Nyx::OrbitCameraComponent{});
		rig.Input.GetUser().SetContextActive(rig.Orbit.GetFreeLookContext(), true);
		rig.Frame();
		rig.Frame();
		rig.TakeLook();
		rig.MoveMouse(glm::vec2(100.0f, 0.0f));
		rig.Frame();
		Require(std::abs(rig.TakeLook().x - 1.0f) < 1e-4f, "The asset's sensitivity turns 100 pixels into 1 radian");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		TestTheContextsAreOnOnlyWithAnOrbitCamera();
		TestHoldToLook();
		TestFreeLookIsAskedForWithAContext();
		TestTheStickLooksAndTheWheelZooms();
		TestTheDefaultAssetHasThem();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Orbit camera input test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All orbit camera input tests passed.\n";
	return 0;
}

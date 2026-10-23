// The game's fly camera (UpdateFlyCameras()) on a world without a window: an entity without a
// FlyCameraComponent stays where it is and the fly camera's contexts stay off; with one, the
// keyboard and the mouse move and turn it. The component is saved with the scene and loaded back.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "FlyCameraComponent.h"
#include "FlyCameras.h"
#include "InputSystem.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"
#include "VirtualInputBackend.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Input;
	using Nyx::Tests::Require;

	constexpr uint64_t Ms = 1000;
	constexpr float FrameSeconds = 0.016f;

	// An input system with a virtual keyboard and mouse, the game's fly camera input, and a world
	// with a camera entity
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		uint64_t NowUs = 1000 * Ms;
		FlyCameraInput Fly{ Input, FlyCameraInput::Settings{} };
		Nyx::Registry World;
		Nyx::Entity Camera;

		Rig()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
			Camera = World.CreateEntity();
			World.Add<Nyx::TransformComponent>(Camera, Nyx::TransformComponent{});
			World.Add<Nyx::CameraComponent>(Camera, Nyx::CameraComponent{});
		}

		// A frame 16 ms after the last, as the game runs it: the input, then the fly cameras
		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			UpdateFlyCameras(World, Fly, FrameSeconds);
			Input.EndFrame();
		}

		bool IsFlying() const
		{
			return Input.GetUser().IsContextActive(Fly.GetFlyContext());
		}

		glm::vec3 Position() const
		{
			return World.Get<Nyx::TransformComponent>(Camera).Position;
		}
	};

	bool Near(float a, float b)
	{
		return std::abs(a - b) <= 1e-4f;
	}

	void TestWithoutTheComponentNothingFlies()
	{
		Rig rig;
		rig.Frame();
		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.NowUs + 1 * Ms);
		for (int frame = 0; frame < 10; ++frame)
		{
			rig.Frame();
		}
		Require(!rig.IsFlying(), "Without a FlyCameraComponent the fly context is off");
		Require(rig.Position() == glm::vec3(0.0f), "Without a FlyCameraComponent W doesn't move the camera");
	}

	void TestTheComponentFliesItsEntity()
	{
		Rig rig;
		rig.World.Add<Nyx::FlyCameraComponent>(rig.Camera, Nyx::FlyCameraComponent{ .MoveSpeed = 2.0f });
		rig.Frame();
		rig.Frame();
		Require(rig.IsFlying(), "With a FlyCameraComponent the fly context is on");

		// W moves forward, along -Z, at the component's speed
		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Position().z < 0.0f && Near(rig.Position().x, 0.0f) && Near(rig.Position().y, 0.0f), "W moves the camera forward");
		Require(Near(rig.Position().z, -2.0f * FrameSeconds), "It moves at the component's speed");
		rig.Backend.Release(rig.Keyboard, Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Frame();

		// The right mouse button held looks around with the mouse, from the frame after the click
		const glm::quat before = rig.World.Get<Nyx::TransformComponent>(rig.Camera).Rotation;
		rig.Backend.Press(rig.Mouse, Nyx::Input::Mouse(EMouseControl::Right), rig.NowUs + 1 * Ms);
		rig.Frame();
		rig.Frame();
		Require(rig.World.Get<Nyx::FlyCameraComponent>(rig.Camera).bLooking, "Holding the right mouse button looks around");
		Require(rig.Input.GetUser().IsContextActive(rig.Fly.GetMouseLookContext()), "The mouse look context is on while looking");
		rig.Backend.SetValue(rig.Mouse, Nyx::Input::Mouse(EMouseControl::Delta), glm::vec2(100.0f, 0.0f), rig.NowUs + 1 * Ms);
		rig.Frame();
		const glm::quat after = rig.World.Get<Nyx::TransformComponent>(rig.Camera).Rotation;
		Require(std::abs(glm::dot(before, after)) < 0.9999f, "Moving the mouse while looking turns the camera");

		// Without the component again, the contexts go off
		rig.Backend.Release(rig.Mouse, Nyx::Input::Mouse(EMouseControl::Right), rig.NowUs + 1 * Ms);
		rig.World.Remove<Nyx::FlyCameraComponent>(rig.Camera);
		rig.Frame();
		rig.Frame();
		Require(!rig.IsFlying(), "The fly context goes off with the last FlyCameraComponent");
	}

	void TestTheComponentIsSavedWithTheScene()
	{
		Nyx::Tests::TestDirectory directory("NyxFlyCameraTests");
		const std::filesystem::path path = directory.Path / "FlyCamera.nyxscene";

		Nyx::Registry saved;
		const Nyx::Entity camera = saved.CreateEntity();
		saved.Add<Nyx::TransformComponent>(camera, Nyx::TransformComponent{});
		saved.Add<Nyx::FlyCameraComponent>(camera, Nyx::FlyCameraComponent{ .MoveSpeed = 7.5f, .LookSensitivity = 0.5f, .SpeedScale = 3.0f });
		Require(Nyx::SceneSerializer::SaveToFile(saved, path), "The scene saves");

		Nyx::Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(path, loaded), "The scene loads");
		int count = 0;
		loaded.Each<Nyx::FlyCameraComponent>(
			[&](Nyx::Entity, Nyx::FlyCameraComponent& fly)
			{
				++count;
				Require(Near(fly.MoveSpeed, 7.5f) && Near(fly.LookSensitivity, 0.5f), "The settings come back");
				Require(Near(fly.SpeedScale, 1.0f) && !fly.bLooking, "What the game made of them doesn't");
			});
		Require(count == 1, "The FlyCameraComponent comes back");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		TestWithoutTheComponentNothingFlies();
		TestTheComponentFliesItsEntity();
		TestTheComponentIsSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Fly camera test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All fly camera tests passed.\n";
	return 0;
}

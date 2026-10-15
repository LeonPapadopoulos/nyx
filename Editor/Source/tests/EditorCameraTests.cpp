// The editor's fly camera without a window or renderer: virtual keyboard and mouse, stand-in
// scene views, and cameras kept in a map.
#include "EditorCameraController.h"
#include "Log.h"
#include "VirtualInputBackend.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Editor;
	using namespace Nyx::Input;

	using Nyx::Tests::Require;

	bool Near(float a, float b)
	{
		return std::abs(a - b) < 1e-4f;
	}

	struct Camera
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Rotation{ 0.0f };
	};

	class MapCameras final : public IEditorCameras
	{
	public:
		bool GetCamera(uint64_t sceneViewId, glm::vec3& outPosition, glm::vec3& outRotationRadians) const override
		{
			const auto found = Cameras.find(sceneViewId);
			if (found == Cameras.end())
			{
				return false;
			}
			outPosition = found->second.Position;
			outRotationRadians = found->second.Rotation;
			return true;
		}

		void SetCamera(uint64_t sceneViewId, const glm::vec3& position, const glm::vec3& rotationRadians) override
		{
			Cameras[sceneViewId] = Camera{ position, rotationRadians };
		}

		std::map<uint64_t, Camera> Cameras;
	};

	constexpr uint64_t MainView = 1;
	constexpr uint64_t FloatingView = 2;
	constexpr float FrameTime = 0.1f;

	// Two scene views, the second in an OS window of its own, and a virtual keyboard and mouse
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		MapCameras Cameras;
		EditorCameraController Controller;
		int FloatingWindow = 0;
		uint64_t TimeUs = 1000;

		EditorCameraController::ViewState Main{ MainView, false, false, true, nullptr };
		EditorCameraController::ViewState Floating{ FloatingView, false, false, true, &FloatingWindow };

		Rig()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", TimeUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", TimeUs);
			Cameras.SetCamera(MainView, glm::vec3(0.0f), glm::vec3(0.0f));
			Cameras.SetCamera(FloatingView, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));
		}

		uint64_t Later()
		{
			TimeUs += 1000;
			return TimeUs;
		}

		// One frame as the editor runs it: input, the controller, then the panels drawn
		void Frame(EditorCameraController::Blockers blockers = {})
		{
			Input.BeginFrame(TimeUs += 100000);
			Controller.Update(Input, FrameTime, blockers, Cameras);
			Controller.ReportView(Main);
			Controller.ReportView(Floating);
			Input.EndFrame();
		}

		const Camera& Of(uint64_t view)
		{
			return Cameras.Cameras.at(view);
		}
	};

	void TestMovesHoveredView()
	{
		Rig rig;
		rig.Main.bHovered = true;
		rig.Frame(); // The panels report for the first time

		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.Later());
		rig.Frame();

		const float step = rig.Controller.GetSettings().StartSpeed * FrameTime;
		Require(Near(rig.Of(MainView).Position.z, -step), "W should move the hovered view forward (-Z) by speed x time");
		Require(rig.Of(FloatingView).Position == glm::vec3(10.0f, 0.0f, 0.0f), "The other view shouldn't move");
	}

	void TestFocusedViewWhenNoneIsHovered()
	{
		Rig rig;
		rig.Floating.bFocused = true;
		rig.Frame();

		rig.Backend.Press(rig.Keyboard, Key(EKey::E), rig.Later());
		rig.Frame();
		Require(rig.Of(FloatingView).Position.y > 0.0f, "E should move the focused view up");
		Require(rig.Of(MainView).Position == glm::vec3(0.0f), "The view without hover or focus shouldn't move");
	}

	void TestNoMovementWhileTyping()
	{
		Rig rig;
		rig.Main.bHovered = true;
		rig.Frame();

		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.Later());
		rig.Frame({ .bTypingText = true });
		Require(rig.Of(MainView).Position == glm::vec3(0.0f), "Keys typed into a text field shouldn't move the camera");
	}

	void TestGameViewIsLeftAlone()
	{
		Rig rig;
		rig.Main.bHovered = true;
		rig.Main.bFreeCamera = false;
		rig.Frame();

		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.Later());
		rig.Frame();
		Require(rig.Of(MainView).Position == glm::vec3(0.0f), "A view showing the game's camera shouldn't fly");
	}

	void TestInspectingSourcesBlocks()
	{
		Rig rig;
		rig.Main.bHovered = true;
		rig.Frame();

		rig.Backend.Press(rig.Keyboard, Key(EKey::W), rig.Later());
		rig.Frame({ .bInspectingSources = true });
		Require(rig.Of(MainView).Position == glm::vec3(0.0f), "Nothing moves while inspecting sources");
	}

	void TestMouseLook()
	{
		Rig rig;
		rig.Floating.bHovered = true;
		rig.Frame();

		// The click into the view starts looking; its own mouse moves don't turn yet
		rig.Backend.Press(rig.Mouse, Mouse(EMouseControl::Right), rig.Later());
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Delta), { 50.0f, 0.0f }, rig.Later());
		rig.Frame();
		Require(rig.Controller.GetLookingViewId() == FloatingView, "A right click into the view should start looking");
		Require(rig.Of(FloatingView).Rotation == glm::vec3(0.0f), "The click's frame shouldn't turn the camera");
		Require(rig.Input.GetCursor().Mode == ECursorMode::Captured, "Looking captures the cursor");
		Require(rig.Input.GetCursor().Window == &rig.FloatingWindow, "The cursor should be captured in the view's own OS window");

		// The view isn't hovered any more (the captured cursor wanders): it keeps looking
		rig.Floating.bHovered = false;
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Delta), { 100.0f, -20.0f }, rig.Later());
		rig.Frame();
		const float sensitivity = rig.Controller.GetSettings().LookSensitivity;
		Require(Near(rig.Of(FloatingView).Rotation.y, -100.0f * sensitivity), "Moving right should turn left-handed around Y");
		Require(Near(rig.Of(FloatingView).Rotation.x, 20.0f * sensitivity), "Moving up should look up");

		// Pitch stops short of straight up
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Delta), { 0.0f, -100000.0f }, rig.Later());
		rig.Frame();
		Require(rig.Of(FloatingView).Rotation.x <= glm::radians(89.0f) + 1e-4f, "Pitch should stop at 89 degrees");

		rig.Backend.Release(rig.Mouse, Mouse(EMouseControl::Right), rig.Later());
		rig.Frame();
		Require(rig.Controller.GetLookingViewId() == 0, "Letting go of the button should stop looking");
		Require(rig.Input.GetCursor().Mode == ECursorMode::Free, "The cursor should be free again");
	}

	// A right click elsewhere, e.g. in the outliner, while a view only has the focus
	void TestRightClickOutsideDoesntLook()
	{
		Rig rig;
		rig.Main.bFocused = true;
		rig.Frame();

		rig.Backend.Press(rig.Mouse, Mouse(EMouseControl::Right), rig.Later());
		rig.Frame();
		Require(rig.Controller.GetLookingViewId() == 0, "A right click outside the view shouldn't start looking");
		Require(rig.Input.GetCursor().Mode == ECursorMode::Free, "The cursor should stay free");
	}

	void TestLookEndsWhenViewCloses()
	{
		Rig rig;
		rig.Main.bHovered = true;
		rig.Frame();
		rig.Backend.Press(rig.Mouse, Mouse(EMouseControl::Right), rig.Later());
		rig.Frame();
		Require(rig.Controller.GetLookingViewId() == MainView, "Expected to be looking");

		// The panel is closed: it doesn't report any more
		rig.Main.SceneViewId = 99;
		rig.Frame();
		rig.Frame();
		Require(rig.Controller.GetLookingViewId() == 0, "Looking should end with its view");
	}

	void TestWheelChangesSpeed()
	{
		Rig rig;
		const EditorCameraController::Settings& settings = rig.Controller.GetSettings();
		rig.Main.bFocused = true;
		rig.Frame();

		// Only focused, the cursor is elsewhere (e.g. scrolling the outliner): no change
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Wheel), { 0.0f, 1.0f }, rig.Later());
		rig.Frame();
		Require(Near(rig.Controller.GetMoveSpeed(), settings.StartSpeed), "Scrolling outside the view shouldn't change the speed");

		rig.Main.bHovered = true;
		rig.Frame();
		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Wheel), { 0.0f, 2.0f }, rig.Later());
		rig.Frame();
		Require(Near(rig.Controller.GetMoveSpeed(), settings.StartSpeed * settings.SpeedStep * settings.SpeedStep),
			"Two notches over the view should speed up twice");

		rig.Backend.SetValue(rig.Mouse, Mouse(EMouseControl::Wheel), { 0.0f, -1000.0f }, rig.Later());
		rig.Frame();
		Require(Near(rig.Controller.GetMoveSpeed(), settings.MinSpeed), "The speed should stop at its minimum");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestMovesHoveredView();
		TestFocusedViewWhenNoneIsHovered();
		TestNoMovementWhileTyping();
		TestGameViewIsLeftAlone();
		TestInspectingSourcesBlocks();
		TestMouseLook();
		TestRightClickOutsideDoesntLook();
		TestLookEndsWhenViewCloses();
		TestWheelChangesSpeed();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor camera test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor camera tests passed.\n";
	return 0;
}

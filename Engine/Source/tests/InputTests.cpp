// The input system without a window: virtual devices feed events with made-up times into frames,
// and the frames are checked.
#include "InputSystem.h"
#include "Log.h"
#include "VirtualInputBackend.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Input;

	using Nyx::Tests::Require;

	constexpr ControlId W = Key(EKey::W);
	constexpr ControlId Space = Key(EKey::Space);
	constexpr ControlId RightButton = Mouse(EMouseControl::Right);
	constexpr ControlId Delta = Mouse(EMouseControl::Delta);
	constexpr ControlId Wheel = Mouse(EMouseControl::Wheel);

	// An input system with one virtual keyboard and mouse, connected in frame 1
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		uint64_t TimeUs = 1000;

		Rig()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", TimeUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", TimeUs);
			NextFrame();
		}

		// Moves the clock on and returns the new time, for events within a frame
		uint64_t At(uint64_t microsecondsLater)
		{
			TimeUs += microsecondsLater;
			return TimeUs;
		}

		// A frame of 16 ms
		const InputFrame& NextFrame()
		{
			Input.BeginFrame(At(16000));
			return Input.GetFrame();
		}
	};

	void TestControlPaths()
	{
		for (uint16_t index = 0; index < GetControlCount(EDeviceKind::Keyboard); ++index)
		{
			const ControlId key{ EDeviceKind::Keyboard, index };
			const std::optional<ControlId> parsed = ParseControlPath(GetControlPath(key));
			Require(parsed && *parsed == key, "Key path doesn't round trip: " + GetControlPath(key));
		}
		for (const EDeviceKind kind : { EDeviceKind::Mouse, EDeviceKind::Gamepad })
		{
			for (uint16_t index = 0; index < GetControlCount(kind); ++index)
			{
				const ControlId control{ kind, index };
				const std::optional<ControlId> parsed = ParseControlPath(GetControlPath(control));
				Require(parsed && *parsed == control, "Path doesn't round trip: " + GetControlPath(control));
			}
		}
		Require(GetControlPath(Gamepad(EGamepadControl::FaceSouth)) == "Gamepad/FaceSouth", "FaceSouth's path");
		Require(ParseControlPath("gamepad/righttrigger") == Gamepad(EGamepadControl::RightTrigger), "Gamepad paths ignore case too");

		Require(IsButton(Gamepad(EGamepadControl::TouchpadButton)) && IsButton(Gamepad(EGamepadControl::DpadLeft)), "Pad buttons are buttons");
		Require(!IsButton(Gamepad(EGamepadControl::LeftStick)) && !IsButton(Gamepad(EGamepadControl::RightTrigger)) &&
				!IsButton(Gamepad(EGamepadControl::Gyro)) && !IsButton(Gamepad(EGamepadControl::Touchpad0)),
			"Sticks, triggers, sensors and touchpad fingers are analog");
		Require(!IsRelative(Gamepad(EGamepadControl::LeftStick)), "Sticks are absolute");

		Require(GetControlPath(W) == "Keyboard/W", "W's path is " + GetControlPath(W));
		Require(GetControlPath(Key(EKey::X)) == "Keyboard/X", "X's path is " + GetControlPath(Key(EKey::X)));
		Require(GetControlPath(Delta) == "Mouse/Delta", "Delta's path is " + GetControlPath(Delta));
		Require(ParseControlPath("keyboard/space") == Space, "Paths should be read ignoring case");
		Require(!ParseControlPath("Keyboard/Nope"), "An unknown key was read");
		Require(!ParseControlPath("Joystick/Throttle"), "An unknown device was read");
		Require(!ParseControlPath("Keyboard"), "A path without a control was read");

		Require(IsButton(W) && IsButton(RightButton), "Keys and mouse buttons are buttons");
		Require(IsRelative(Delta) && IsRelative(Wheel) && !IsButton(Delta), "Delta and Wheel are relative");
	}

	void TestDevicesConnect()
	{
		Rig rig;
		const InputFrame& frame = rig.Input.GetFrame();
		Require(frame.GetDevices().size() == 2, "Expected the keyboard and the mouse");
		Require(frame.FindDevice(rig.Keyboard) && frame.FindDevice(rig.Keyboard)->Info.Name == "Keyboard", "The keyboard is missing");
		Require(frame.FindDevice(rig.Mouse) && frame.FindDevice(rig.Mouse)->Info.Backend == "Virtual", "The mouse is missing");
	}

	void TestHeldKey()
	{
		Rig rig;
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		const uint64_t pressedAt = rig.TimeUs;

		const InputFrame& first = rig.NextFrame();
		Require(first.IsDown(W) && first.WasPressed(W) && !first.WasReleased(W), "W should be down and pressed in its first frame");
		Require(first.GetState(W).LastPressUs == pressedAt, "The press time should be the event's, not the frame's");

		const InputFrame& second = rig.NextFrame();
		Require(second.IsDown(W) && !second.WasPressed(W), "W should stay down without being pressed again");
		Require(second.GetState(W).PreviousValue.x == 1.0f, "W was down at the end of the frame before");

		rig.Backend.Release(rig.Keyboard, W, rig.At(1000));
		const InputFrame& third = rig.NextFrame();
		Require(!third.IsDown(W) && third.WasReleased(W) && !third.WasPressed(W), "W should be released");
	}

	// The reason the input system counts presses instead of polling: a tap between two frames
	void TestTapWithinOneFrame()
	{
		Rig rig;
		rig.Backend.Press(rig.Keyboard, Space, rig.At(1000));
		rig.Backend.Release(rig.Keyboard, Space, rig.At(2000));

		const InputFrame& frame = rig.NextFrame();
		Require(frame.WasPressed(Space) && frame.WasReleased(Space), "A tap within a frame should be pressed and released");
		Require(!frame.IsDown(Space), "After the tap, Space is up");

		const InputFrame& next = rig.NextFrame();
		Require(!next.WasPressed(Space) && !next.WasReleased(Space), "The tap belongs to one frame only");

		// Two taps in one frame
		rig.Backend.Press(rig.Keyboard, Space, rig.At(1000));
		rig.Backend.Release(rig.Keyboard, Space, rig.At(1000));
		rig.Backend.Press(rig.Keyboard, Space, rig.At(1000));
		rig.Backend.Release(rig.Keyboard, Space, rig.At(1000));
		const ControlState twice = rig.NextFrame().GetState(Space);
		Require(twice.Presses == 2 && twice.Releases == 2, "Two taps in a frame should count twice");
	}

	// A press reported again while the key is down (e.g. by another window) isn't a second press
	void TestRepeatedPressCountsOnce()
	{
		Rig rig;
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		Require(rig.NextFrame().GetState(W).Presses == 1, "A second press while down shouldn't count");

		rig.Backend.Release(rig.Keyboard, W, rig.At(1000));
		rig.Backend.Release(rig.Keyboard, W, rig.At(1000));
		Require(rig.NextFrame().GetState(W).Releases == 1, "A second release while up shouldn't count");
	}

	void TestRelativeControlsAddUp()
	{
		Rig rig;
		rig.Backend.SetValue(rig.Mouse, Delta, { 3.0f, -1.0f }, rig.At(1000));
		rig.Backend.SetValue(rig.Mouse, Delta, { 2.0f, 4.0f }, rig.At(1000));
		rig.Backend.SetValue(rig.Mouse, Wheel, { 0.0f, 1.0f }, rig.At(1000));
		rig.Backend.SetValue(rig.Mouse, Wheel, { 0.0f, 1.0f }, rig.At(1000));

		const InputFrame& frame = rig.NextFrame();
		Require(frame.GetValue(Delta) == glm::vec3(5.0f, 3.0f, 0.0f), "Mouse/Delta should be the sum of the frame");
		Require(frame.GetValue(Wheel) == glm::vec3(0.0f, 2.0f, 0.0f), "Mouse/Wheel should be the sum of the frame");

		const InputFrame& next = rig.NextFrame();
		Require(next.GetValue(Delta) == glm::vec3(0.0f), "Mouse/Delta should start at zero each frame");
		Require(next.GetState(Delta).PreviousValue == glm::vec3(5.0f, 3.0f, 0.0f), "The previous frame's delta should be kept");
	}

	// Two backends' events are applied in the order they happened, not backend by backend
	void TestEventsInTimeOrder()
	{
		VirtualInputBackend first;
		VirtualInputBackend second;
		InputSystem input;
		input.AddBackend(first);
		input.AddBackend(second);

		const InputDeviceId keyboard = first.AddDevice(EDeviceKind::Keyboard, "Keyboard", 100);
		first.Press(keyboard, W, 300);
		const InputDeviceId otherKeyboard = second.AddDevice(EDeviceKind::Keyboard, "Other keyboard", 200);
		second.Press(otherKeyboard, Space, 250);
		input.BeginFrame(1000);

		const std::vector<InputEvent>& events = input.GetFrame().GetEvents();
		Require(events.size() == 4, "Expected four events");
		for (size_t i = 1; i < events.size(); ++i)
		{
			Require(events[i - 1].TimeUs <= events[i].TimeUs, "Events should be applied in time order");
		}
		Require(input.GetFrame().IsDown(W) && input.GetFrame().IsDown(Space), "Both keys should be down");
	}

	void TestTwoKeyboardsHoldTogether()
	{
		Rig rig;
		const InputDeviceId second = rig.Backend.AddDevice(EDeviceKind::Keyboard, "Second keyboard", rig.At(1000));
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		rig.Backend.Press(second, W, rig.At(1000));
		rig.NextFrame();

		rig.Backend.Release(rig.Keyboard, W, rig.At(1000));
		const InputFrame& frame = rig.NextFrame();
		Require(frame.IsDown(W), "W is still held on the second keyboard");
		Require(!frame.GetState(rig.Keyboard, W).Value.x && frame.GetState(second, W).Value.x, "Each keyboard keeps its own state");
	}

	// A device unplugged while a button is held doesn't leave the button stuck
	void TestDeviceRemovedWhileHeld()
	{
		Rig rig;
		rig.Backend.Press(rig.Mouse, RightButton, rig.At(1000));
		rig.NextFrame();

		rig.Backend.RemoveDevice(rig.Mouse, rig.At(1000));
		const InputFrame& removed = rig.NextFrame();
		Require(!removed.IsDown(RightButton) && removed.WasReleased(RightButton), "The held button should be released with its device");
		Require(removed.FindDevice(rig.Mouse) && removed.FindDevice(rig.Mouse)->bRemoved, "The device is still there, as removed, in its last frame");

		rig.Backend.Press(rig.Mouse, RightButton, rig.At(1000));
		const InputFrame& after = rig.NextFrame();
		Require(!after.FindDevice(rig.Mouse), "The device should be gone the frame after");
		Require(!after.IsDown(RightButton), "Events of a removed device are ignored");
	}

	// Keys let go of while another program has the focus never arrive: losing the focus releases
	// everything, and without it nothing is pressed
	void TestFocusLossReleasesEverything()
	{
		Rig rig;
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		rig.Backend.Press(rig.Mouse, RightButton, rig.At(1000));
		rig.NextFrame();

		rig.Backend.SetFocus(false, rig.At(1000));
		const InputFrame& unfocused = rig.NextFrame();
		Require(!unfocused.HasFocus(), "The frame should know the focus is gone");
		Require(!unfocused.IsDown(W) && unfocused.WasReleased(W), "W should be released with the focus");
		Require(!unfocused.IsDown(RightButton) && unfocused.WasReleased(RightButton), "The mouse button should be released with the focus");

		rig.Backend.Press(rig.Keyboard, Space, rig.At(1000));
		Require(!rig.NextFrame().IsDown(Space), "Without the focus, nothing is pressed");

		rig.Backend.SetFocus(true, rig.At(1000));
		rig.Backend.Press(rig.Keyboard, Space, rig.At(1000));
		Require(rig.NextFrame().IsDown(Space), "With the focus back, keys count again");
	}

	// Sticks report where they are: the frame keeps the last value. Of two pads, the stick pushed
	// further counts, not the sum.
	void TestGamepadAnalogValues()
	{
		Rig rig;
		const InputDeviceId first = rig.Backend.AddDevice(EDeviceKind::Gamepad, "First pad", rig.At(1000));
		const InputDeviceId second = rig.Backend.AddDevice(EDeviceKind::Gamepad, "Second pad", rig.At(1000));
		const ControlId stick = Gamepad(EGamepadControl::LeftStick);
		rig.Backend.SetValue(first, stick, glm::vec3(0.2f, 0.1f, 0.0f), rig.At(1000));
		rig.Backend.SetValue(first, stick, glm::vec3(0.5f, -0.4f, 0.0f), rig.At(1000));
		rig.Backend.SetValue(second, stick, glm::vec3(-0.1f, 0.0f, 0.0f), rig.At(1000));

		const InputFrame& frame = rig.NextFrame();
		Require(frame.GetValue(stick) == glm::vec3(0.5f, -0.4f, 0.0f), "The stick pushed further counts, as last reported");
		Require(rig.NextFrame().GetValue(stick) == glm::vec3(0.5f, -0.4f, 0.0f), "A stick keeps its value until it reports another");

		// A pad that goes away doesn't leave its stick pushed
		rig.Backend.RemoveDevice(first, rig.At(1000));
		Require(rig.NextFrame().GetValue(stick) == glm::vec3(-0.1f, 0.0f, 0.0f), "The removed pad's stick is gone, the other one counts");
	}

	// Pads belong to no window: they keep reporting without the focus (the Unfocused context takes
	// them in the router), while keys and mouse buttons are released
	void TestGamepadsWithoutFocus()
	{
		Rig rig;
		const InputDeviceId pad = rig.Backend.AddDevice(EDeviceKind::Gamepad, "Pad", rig.At(1000));
		rig.Backend.Press(pad, Gamepad(EGamepadControl::FaceSouth), rig.At(1000));
		rig.Backend.Press(rig.Keyboard, W, rig.At(1000));
		rig.NextFrame();

		rig.Backend.SetFocus(false, rig.At(1000));
		rig.Backend.Press(pad, Gamepad(EGamepadControl::FaceNorth), rig.At(1000));
		const InputFrame& frame = rig.NextFrame();
		Require(!frame.IsDown(W), "Keys are released with the focus");
		Require(frame.IsDown(Gamepad(EGamepadControl::FaceSouth)) && frame.IsDown(Gamepad(EGamepadControl::FaceNorth)),
			"Pad buttons stay down and still come in");
	}

	void TestDeviceChanges()
	{
		Rig rig;
		const InputDeviceId pad = rig.Backend.AddDevice(EDeviceKind::Gamepad, "Pad", rig.At(1000));
		rig.NextFrame();

		InputDeviceInfo info = rig.Input.GetFrame().FindDevice(pad)->Info;
		info.Model = EGamepadModel::DualSense;
		info.BatteryPercent = 40;
		info.Kind = EDeviceKind::Keyboard; // Kept as it was
		rig.Backend.SetDeviceInfo(pad, info, rig.At(1000));
		const InputFrame::DeviceState* device = rig.NextFrame().FindDevice(pad);
		Require(device && device->Info.Model == EGamepadModel::DualSense && device->Info.BatteryPercent == 40, "A device's info changes");
		Require(device->Info.Kind == EDeviceKind::Gamepad && device->Controls.size() == GetControlCount(EDeviceKind::Gamepad),
			"Its kind and controls stay");
	}

	// The cursor follows the context stack (NyxInputRouterTests); here only that it reaches the
	// backends at the end of every frame
	void TestCursorAppliedEachFrame()
	{
		Rig rig;
		rig.Input.EndFrame();
		rig.NextFrame();
		rig.Input.EndFrame();
		Require(rig.Backend.GetCursorApplications() == 2, "The cursor should be applied at the end of each frame");
		Require(rig.Backend.GetAppliedCursor().Mode == ECursorMode::Free, "Without a context saying otherwise, the cursor is free");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestControlPaths();
		TestDevicesConnect();
		TestHeldKey();
		TestTapWithinOneFrame();
		TestRepeatedPressCountsOnce();
		TestRelativeControlsAddUp();
		TestEventsInTimeOrder();
		TestTwoKeyboardsHoldTogether();
		TestDeviceRemovedWhileHeld();
		TestFocusLossReleasesEverything();
		TestGamepadAnalogValues();
		TestGamepadsWithoutFocus();
		TestDeviceChanges();
		TestCursorAppliedEachFrame();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input tests passed.\n";
	return 0;
}

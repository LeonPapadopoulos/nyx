// What the Input window asks of the input system, without a window: who gets a control with a
// stack (the control map, What if), every binding of a control (Press to find), stick
// calibration's suggestions, and the history the timeline and events show.
#include "InputHistory.h"
#include "InputInspection.h"
#include "Log.h"
#include "VirtualInputBackend.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Input;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) < 1e-4f;
	}

	constexpr uint64_t Ms = 1000;

	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;

		ActionHandle Jump;
		ActionHandle Confirm;
		ActionHandle Copy;
		ContextHandle OnFoot;
		ContextHandle Menu;
		ContextHandle Debug;

		Rig()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);

			Jump = Input.AddAction({ "Jump" });
			Confirm = Input.AddAction({ "Confirm" });
			Copy = Input.AddAction({ "Copy" });
			OnFoot = Input.AddContext({ .Name = "OnFoot",
				.Bindings = { InputBinding::ForControl(Jump, Gamepad(EGamepadControl::FaceSouth)), InputBinding::ForControl(Jump, Key(EKey::Space)) } });
			Menu = Input.AddContext({ .Name = "Menu", .Priority = 100, .Bindings = { InputBinding::ForControl(Confirm, Gamepad(EGamepadControl::FaceSouth)) } });
			InputBinding copy = InputBinding::ForControl(Copy, Key(EKey::C));
			copy.Modifiers = EModifierKeys::Ctrl;
			Debug = Input.AddContext({ .Name = "Debug", .Priority = 800, .Bindings = { copy } });
			Input.GetUser().SetContextActive(OnFoot, true);
			Frame();
		}

		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			Input.EndFrame();
		}
	};

	void TestResolveControl()
	{
		Rig rig;
		const ControlId south = Gamepad(EGamepadControl::FaceSouth);

		std::vector<ControlUse> uses = ResolveControl(rig.Input, rig.Input.GetUser().GetStack(), south);
		Require(uses.size() == 1 && uses[0].Context == rig.OnFoot && uses[0].Action == rig.Jump && uses[0].bTakes, "Only OnFoot: it gets South");

		// What if the menu were open
		const std::vector<ContextHandle> contexts = { rig.OnFoot, rig.Menu };
		const std::vector<InputUser::StackEntry> whatIf = MakeStack(rig.Input, contexts);
		Require(whatIf.size() == 2 && whatIf[0].Context == rig.Menu, "What if: the menu on top, by priority");
		uses = ResolveControl(rig.Input, whatIf, south);
		Require(uses.size() == 1 && uses[0].Context == rig.Menu && uses[0].Action == rig.Confirm, "With the menu, Confirm gets South and Jump doesn't");

		// A binding with modifiers sees the key but doesn't take it: below, C still gets through
		rig.Input.GetUser().SetContextActive(rig.Debug, true);
		const ActionHandle crouch = rig.Input.AddAction({ "Crouch" });
		const ContextHandle game = rig.Input.AddContext({ .Name = "Game", .Bindings = { InputBinding::ForControl(crouch, Key(EKey::C)) } });
		rig.Input.GetUser().SetContextActive(game, true);
		rig.Frame();
		uses = ResolveControl(rig.Input, rig.Input.GetUser().GetStack(), Key(EKey::C));
		Require(uses.size() == 2 && uses[0].Action == rig.Copy && !uses[0].bTakes && uses[1].Action == crouch && uses[1].bTakes,
			"Ctrl+C sees C without taking it; Crouch gets it");

		// A context taking the whole device
		rig.Backend.SetFocus(false, rig.NowUs + 1);
		rig.Frame();
		uses = ResolveControl(rig.Input, rig.Input.GetUser().GetStack(), south);
		Require(uses.size() == 1 && uses[0].Context == rig.Input.GetUnfocusedContext() && uses[0].BindingIndex == NoBinding,
			"Without the focus, Unfocused takes the pad");
	}

	void TestFindBindingsOf()
	{
		Rig rig;
		const std::vector<ControlUse> uses = FindBindingsOf(rig.Input, Gamepad(EGamepadControl::FaceSouth));
		Require(uses.size() == 2, "South is bound in OnFoot and in the menu, on or off");
		Require(FindBindingsOf(rig.Input, Key(EKey::Space)).size() == 1, "Space once");
		Require(FindBindingsOf(rig.Input, Key(EKey::LeftControl)).empty(), "Modifier keys aren't a binding's controls");
	}

	void TestCalibrationSuggestions()
	{
		// A stick resting up to 0.05 from the middle: 0.05 * 1.2 + 0.02 = 0.08
		const std::vector<glm::vec2> resting = { { 0.01f, 0.0f }, { 0.03f, 0.04f }, { -0.02f, 0.01f } };
		const StickCalibration::Result drift = StickCalibration::SuggestInnerDeadZone(resting);
		Require(drift.Suggested && Near(*drift.Suggested, 0.08f), "Drift 0.05 suggests 0.08, got " + std::to_string(drift.Suggested.value_or(-1.0f)));
		Require(Near(*StickCalibration::SuggestInnerDeadZone(std::vector<glm::vec2>{ { 0.0f, 0.0f } }).Suggested, 0.05f), "At least 0.05");

		// Turned round the edge, reaching only 0.97 straight up (over more than one of the 32 directions)
		std::vector<glm::vec2> circle;
		for (int step = 0; step < 360; ++step)
		{
			const float angle = static_cast<float>(step) * 3.14159265f / 180.0f;
			circle.push_back(glm::vec2(std::cos(angle), std::sin(angle)) * (step >= 70 && step <= 110 ? 0.97f : 1.0f));
		}
		const StickCalibration::Result range = StickCalibration::SuggestOuterDeadZone(circle);
		Require(range.Suggested && Near(*range.Suggested, 0.96f), "The edge at 0.97 suggests 0.96, got " + std::to_string(range.Suggested.value_or(-1.0f)));

		// Half way round isn't enough
		std::vector<glm::vec2> half(circle.begin(), circle.begin() + 180);
		const StickCalibration::Result partial = StickCalibration::SuggestOuterDeadZone(half);
		Require(!partial.Suggested && partial.Explanation.find("turn the stick all the way round") != std::string::npos,
			"Half a turn gives no suggestion, and says why: " + partial.Explanation);
	}

	void TestCalibrationRun()
	{
		Rig rig;
		StickCalibration calibration;
		calibration.Start(StickCalibration::EMeasure::Drift, rig.Pad, Gamepad(EGamepadControl::LeftStick), rig.NowUs, 0.1f);
		for (int frame = 0; frame < 10 && calibration.IsRunning(); ++frame)
		{
			rig.Backend.SetValue(rig.Pad, Gamepad(EGamepadControl::LeftStick), glm::vec3(0.0f, frame % 2 ? 0.05f : 0.0f, 0.0f), rig.NowUs + 1);
			rig.Frame();
			calibration.Sample(rig.Input.GetFrame());
		}
		Require(!calibration.IsRunning() && calibration.GetResult() && calibration.GetResult()->Suggested, "A drift measurement finishes after its time");
		Require(Near(*calibration.GetResult()->Suggested, 0.08f), "And suggests from what it sampled");

		calibration.Start(StickCalibration::EMeasure::Range, rig.Pad, Gamepad(EGamepadControl::LeftStick), rig.NowUs, 1.0f);
		rig.Backend.RemoveDevice(rig.Pad, rig.NowUs + 1);
		rig.Frame();
		rig.Frame();
		calibration.Sample(rig.Input.GetFrame());
		Require(!calibration.IsRunning() && calibration.GetResult()->Explanation == "The pad was disconnected", "A pad that goes away ends it");
	}

	void TestHistory()
	{
		Rig rig;
		InputHistory history(5, 100);

		rig.Backend.Press(rig.Keyboard, Key(EKey::Space), rig.NowUs + 1);
		rig.Frame();
		history.Record(rig.Input);
		const uint64_t jumpedAt = rig.NowUs;
		rig.Backend.Release(rig.Keyboard, Key(EKey::Space), rig.NowUs + 1);
		for (int frame = 0; frame < 3; ++frame)
		{
			rig.Backend.SetValue(rig.Pad, Gamepad(EGamepadControl::LeftStick), glm::vec3(0.1f * static_cast<float>(frame + 1), 0.0f, 0.0f), rig.NowUs + 1);
			rig.Frame();
			history.Record(rig.Input);
		}

		Require(history.GetFrames().size() == 4, "One frame each Record()");
		Require(history.GetLastTriggeredUs(rig.Jump) == jumpedAt, "When Jump last fired");
		const std::vector<glm::vec3> trail = history.GetTrail(rig.Pad, Gamepad(EGamepadControl::LeftStick));
		Require(trail.size() == 3 && Near(trail.back().x, 0.3f), "The stick's places, oldest first");
		Require(!history.GetFrames().back().Traces.empty() && !history.GetFrames().back().Stack.empty(), "Each frame keeps its traces and stack");

		history.SetPaused(true);
		rig.Frame();
		history.Record(rig.Input);
		Require(history.GetFrames().size() == 4, "Paused, nothing is kept");
		history.SetPaused(false);

		for (int frame = 0; frame < 10; ++frame)
		{
			rig.Frame();
			history.Record(rig.Input);
		}
		Require(history.GetFrames().size() == 5, "At most as many frames as asked for");

		// Output shows among the entries: what the end of the frame before sent
		rig.Input.GetUser().Rumble({ .Low = 1.0f, .DurationMs = 10 });
		rig.Frame();
		history.Record(rig.Input);
		bool bOutput = false;
		for (const InputHistory::Entry& entry : history.GetEntries())
		{
			bOutput = bOutput || (entry.Output && entry.Output->What.starts_with("Rumble"));
		}
		Require(bOutput, "What was sent is in the entries too");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestResolveControl();
		TestFindBindingsOf();
		TestCalibrationSuggestions();
		TestCalibrationRun();
		TestHistory();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input inspection test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input inspection tests passed.\n";
	return 0;
}

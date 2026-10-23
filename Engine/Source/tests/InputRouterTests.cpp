// The router without a window: actions and contexts on an input system with a virtual keyboard
// and mouse, frames at set times, and checks on what the traces say. The value pipeline's steps
// are checked against known values.
#include "InputSystem.h"
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

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
	}

	std::string Text(const glm::vec3& value)
	{
		return FormatValue(value);
	}

	constexpr uint64_t Ms = 1000;
	constexpr uint64_t Second = 1000 * Ms;

	// An input system with a virtual keyboard and mouse, and frames whose times the test chooses
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		uint64_t NowUs = Second;

		Rig()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
			FrameAt(NowUs);
		}

		InputUser& User()
		{
			return Input.GetUser();
		}

		// A context with these bindings, on from the next frame
		ContextHandle AddContext(std::string name, int priority, std::vector<InputBinding> bindings, bool bActive = true)
		{
			InputContextDef context;
			context.Name = std::move(name);
			context.Priority = priority;
			context.Bindings = std::move(bindings);
			const ContextHandle handle = Input.AddContext(std::move(context));
			User().SetContextActive(handle, bActive, { .Reason = "Test" });
			return handle;
		}

		void Press(ControlId control, uint64_t atUs)
		{
			Backend.Press(control.Device == EDeviceKind::Keyboard ? Keyboard : Mouse, control, atUs);
		}

		void Release(ControlId control, uint64_t atUs)
		{
			Backend.Release(control.Device == EDeviceKind::Keyboard ? Keyboard : Mouse, control, atUs);
		}

		void Move(glm::vec2 delta, uint64_t atUs)
		{
			Backend.SetValue(Mouse, Nyx::Input::Mouse(EMouseControl::Delta), delta, atUs);
		}

		void FrameAt(uint64_t atUs)
		{
			NowUs = atUs;
			Input.BeginFrame(atUs);
			Input.EndFrame();
		}

		// 16 ms later
		void Frame()
		{
			FrameAt(NowUs + 16 * Ms);
		}

		const ActionState& Get(ActionHandle action)
		{
			return User().Get(action);
		}

		// The trace of the action's only binding
		const BindingTrace& TraceOf(ActionHandle action)
		{
			const std::vector<const BindingTrace*> traces = User().GetTraces(action);
			Require(traces.size() == 1, "Expected one binding for " + Input.GetAction(action).Name);
			return *traces.front();
		}

		std::string TraceText(ActionHandle action)
		{
			return FormatTrace(Input, TraceOf(action));
		}
	};

	std::string PhaseOf(const ActionState& state)
	{
		return std::string(GetActionPhaseName(state.Phase)) + (state.bTriggered ? " (fired)" : "");
	}

	void RequirePhase(const ActionState& state, EActionPhase phase, bool bFired, const std::string& when)
	{
		Require(state.Phase == phase && state.bTriggered == bFired,
			when + ": expected " + GetActionPhaseName(phase) + (bFired ? " (fired)" : "") + ", got " + PhaseOf(state));
	}

	// ---------------------------------------------------------------------------------------
	// The value pipeline

	void TestDeadZones()
	{
		DeadZoneSettings settings{ .bEnabled = true, .Shape = EDeadZoneShape::Axial, .Inner = 0.1f, .Outer = 0.9f };
		Require(Near(ApplyDeadZone({ 0.05f, 0.5f, 0.0f }, settings), { 0.0f, 0.5f, 0.0f }), "Axial: small x dropped, y rescaled");
		Require(Near(ApplyDeadZone({ -0.95f, 0.0f, 0.0f }, settings), { -1.0f, 0.0f, 0.0f }), "Axial: past the outer edge is full, sign kept");

		settings.Shape = EDeadZoneShape::Radial;
		Require(Near(ApplyDeadZone({ 0.05f, 0.05f, 0.0f }, settings), glm::vec3(0.0f)), "Radial: inside the inner circle is 0");
		Require(Near(ApplyDeadZone({ 0.3f, 0.4f, 0.0f }, settings), { 0.3f, 0.4f, 0.0f }), "Radial: outside it, kept as it is");
		Require(Near(ApplyDeadZone({ 0.57f, 0.76f, 0.0f }, settings), { 0.6f, 0.8f, 0.0f }), "Radial: past the outer edge, length 1");

		settings = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.2f, .Outer = 1.0f };
		Require(Near(ApplyDeadZone({ 0.3f, 0.4f, 0.0f }, settings), { 0.225f, 0.3f, 0.0f }), "Scaled radial: length 0.5 becomes 0.375, direction kept");
		Require(Near(ApplyDeadZone({ 0.1f, 0.1f, 0.0f }, settings), glm::vec3(0.0f)), "Scaled radial: inside the inner circle is 0");

		settings.Shape = EDeadZoneShape::Hybrid;
		Require(Near(ApplyDeadZone({ 0.1f, 0.5f, 0.0f }, settings), { 0.0f, 0.375f, 0.0f }), "Hybrid: a push nearly straight up stays straight up");
	}

	void TestCurves()
	{
		CurveSettings settings{ .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
		Require(Near(ApplyCurve({ 0.3f, 0.4f, 0.0f }, settings), { 0.15f, 0.2f, 0.0f }), "Power 2: length 0.5 becomes 0.25");

		settings = { .bEnabled = true, .Kind = ECurveKind::Custom, .Points = { { 0.0f, 0.0f }, { 0.5f, 0.1f }, { 1.0f, 1.0f } } };
		Require(Near(ApplyCurve({ 0.75f, 0.0f, 0.0f }, settings), { 0.55f, 0.0f, 0.0f }), "Custom: halfway between two points");
		Require(Near(ApplyCurve({ 0.0f, -0.25f, 0.0f }, settings), { 0.0f, -0.05f, 0.0f }), "Custom: the direction is kept");

		settings.Kind = ECurveKind::Linear;
		Require(Near(ApplyCurve({ 0.3f, 0.4f, 0.0f }, settings), { 0.3f, 0.4f, 0.0f }), "Linear leaves the value alone");
	}

	void TestPipelineSteps()
	{
		ValuePipeline pipeline;
		pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(2.0f), .bInvertY = true };
		PipelineState state;
		PipelineResult result = RunPipeline({ 1.0f, 1.0f, 0.0f }, pipeline, state, 0.016f);
		Require(Near(result.Value, { 2.0f, -2.0f, 0.0f }), "Sensitivity 2 with Y inverted");
		Require(result.AfterStep[size_t(EPipelineStep::Sensitivity)] && !result.AfterStep[size_t(EPipelineStep::DeadZone)],
			"Only the steps that are on are recorded");

		// Look acceleration: past 0.9 for 0.25 s, then up to 3x over 0.5 s
		pipeline = {};
		pipeline.LookAcceleration = { .bEnabled = true, .Threshold = 0.9f, .DelaySeconds = 0.25f, .RampSeconds = 0.5f, .MaxMultiplier = 3.0f };
		state = {};
		Require(Near(RunPipeline({ 1.0f, 0.0f, 0.0f }, pipeline, state, 0.25f).Value.x, 1.0f), "No acceleration within the delay");
		Require(Near(RunPipeline({ 1.0f, 0.0f, 0.0f }, pipeline, state, 0.25f).Value.x, 2.0f), "Half way up the ramp: 2x");
		Require(Near(RunPipeline({ 1.0f, 0.0f, 0.0f }, pipeline, state, 0.25f).Value.x, 3.0f), "At the end of the ramp: 3x");
		Require(Near(RunPipeline({ 0.5f, 0.0f, 0.0f }, pipeline, state, 0.25f).Value.x, 0.5f), "Back under the threshold, no acceleration");
		Require(Near(RunPipeline({ 1.0f, 0.0f, 0.0f }, pipeline, state, 0.25f).Value.x, 1.0f), "And the delay starts again");

		pipeline = {};
		pipeline.Smoothing = { .bEnabled = true, .TimeConstantSeconds = 0.1f };
		state = {};
		Require(Near(RunPipeline({ 1.0f, 0.0f, 0.0f }, pipeline, state, 0.1f).Value.x, 1.0f - std::exp(-1.0f)),
			"Smoothing reaches 63% within one time constant");

		pipeline = {};
		pipeline.bMultiplyByDeltaTime = true;
		state = {};
		Require(Near(RunPipeline({ 2.0f, 0.0f, 0.0f }, pipeline, state, 0.5f).Value.x, 1.0f), "Delta time multiplies by the frame time");
	}

	// ---------------------------------------------------------------------------------------
	// Triggers, through the router

	struct OneBinding
	{
		Rig Inputs;
		ActionHandle Action;
		static constexpr ControlId Space = Key(EKey::Space);

		explicit OneBinding(TriggerSettings trigger, EActionValue value = EActionValue::Button)
		{
			Action = Inputs.Input.AddAction({ "Act", value });
			InputBinding binding = InputBinding::ForControl(Action, Space);
			binding.Trigger = trigger;
			Inputs.AddContext("Context", 0, { binding });
			Inputs.Frame();
		}

		const ActionState& FrameAt(uint64_t atUs)
		{
			Inputs.FrameAt(atUs);
			return Inputs.Get(Action);
		}
	};

	void TestDown()
	{
		OneBinding test({ .Kind = ETriggerKind::Down });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 5 * Ms);
		const ActionState& pressed = test.FrameAt(t + 16 * Ms);
		RequirePhase(pressed, EActionPhase::Triggered, true, "Down, pressed");
		Require(pressed.bStarted && pressed.Value.x == 1.0f, "Down: started, value 1");
		RequirePhase(test.FrameAt(t + 32 * Ms), EActionPhase::Triggered, true, "Down, held");
		Require(test.Inputs.Get(test.Action).HeldSeconds > 0.02f, "Down: held for a while");
		test.Inputs.Release(test.Space, t + 40 * Ms);
		RequirePhase(test.FrameAt(t + 48 * Ms), EActionPhase::Completed, false, "Down, let go");
		RequirePhase(test.FrameAt(t + 64 * Ms), EActionPhase::Idle, false, "Down, after");

		// A tap within one frame still fires
		test.Inputs.Press(test.Space, t + 70 * Ms);
		test.Inputs.Release(test.Space, t + 72 * Ms);
		RequirePhase(test.FrameAt(t + 80 * Ms), EActionPhase::Triggered, true, "Down, tap within a frame");
	}

	void TestPressedAndReleased()
	{
		OneBinding pressed({ .Kind = ETriggerKind::Pressed });
		uint64_t t = pressed.Inputs.NowUs;
		pressed.Inputs.Press(pressed.Space, t + 5 * Ms);
		RequirePhase(pressed.FrameAt(t + 16 * Ms), EActionPhase::Triggered, true, "Pressed, press");
		RequirePhase(pressed.FrameAt(t + 32 * Ms), EActionPhase::Idle, false, "Pressed fires once");

		OneBinding released({ .Kind = ETriggerKind::Released });
		t = released.Inputs.NowUs;
		released.Inputs.Press(released.Space, t + 5 * Ms);
		RequirePhase(released.FrameAt(t + 16 * Ms), EActionPhase::Started, false, "Released, press");
		RequirePhase(released.FrameAt(t + 32 * Ms), EActionPhase::Ongoing, false, "Released, held");
		released.Inputs.Release(released.Space, t + 40 * Ms);
		RequirePhase(released.FrameAt(t + 48 * Ms), EActionPhase::Triggered, true, "Released, let go");

		// A tap within one frame is a press and a release
		released.Inputs.Press(released.Space, t + 50 * Ms);
		released.Inputs.Release(released.Space, t + 52 * Ms);
		RequirePhase(released.FrameAt(t + 64 * Ms), EActionPhase::Triggered, true, "Released, tap within a frame");
	}

	void TestToggle()
	{
		OneBinding test({ .Kind = ETriggerKind::Toggle });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 5 * Ms);
		test.Inputs.Release(test.Space, t + 10 * Ms);
		RequirePhase(test.FrameAt(t + 16 * Ms), EActionPhase::Triggered, true, "Toggle, first press");
		RequirePhase(test.FrameAt(t + 32 * Ms), EActionPhase::Triggered, true, "Toggle stays on, let go");
		test.Inputs.Press(test.Space, t + 40 * Ms);
		RequirePhase(test.FrameAt(t + 48 * Ms), EActionPhase::Completed, false, "Toggle, second press");
		RequirePhase(test.FrameAt(t + 64 * Ms), EActionPhase::Idle, false, "Toggle off");
	}

	void TestHold()
	{
		OneBinding test({ .Kind = ETriggerKind::Hold, .HoldSeconds = 0.5f });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 10 * Ms);
		RequirePhase(test.FrameAt(t + 20 * Ms), EActionPhase::Started, false, "Hold, press");
		RequirePhase(test.FrameAt(t + 320 * Ms), EActionPhase::Ongoing, false, "Hold, held 0.31 s");
		Require(test.Inputs.TraceOf(test.Action).Outcome == EBindingOutcome::Waiting, "Hold, waiting");
		Require(test.Inputs.TraceText(test.Action) ==
				"Act   Context (0) - Keyboard/Space - Hold\nraw (1, 0, 0) -> waiting: Hold at 0.31 s of 0.50 s",
			"Hold trace: " + test.Inputs.TraceText(test.Action));

		RequirePhase(test.FrameAt(t + 510 * Ms), EActionPhase::Triggered, true, "Hold, at 0.5 s");
		RequirePhase(test.FrameAt(t + 526 * Ms), EActionPhase::Triggered, false, "Hold fires once");
		test.Inputs.Release(test.Space, t + 600 * Ms);
		RequirePhase(test.FrameAt(t + 610 * Ms), EActionPhase::Completed, false, "Hold, let go after firing");

		// Let go too soon
		test.Inputs.Press(test.Space, t + 700 * Ms);
		RequirePhase(test.FrameAt(t + 710 * Ms), EActionPhase::Started, false, "Hold, second press");
		test.Inputs.Release(test.Space, t + 900 * Ms);
		RequirePhase(test.FrameAt(t + 910 * Ms), EActionPhase::Canceled, false, "Hold, let go at 0.2 s");
		Require(test.Inputs.TraceOf(test.Action).Outcome == EBindingOutcome::Canceled, "The trace says canceled");

		OneBinding repeating({ .Kind = ETriggerKind::Hold, .HoldSeconds = 0.1f, .bHoldRepeats = true });
		const uint64_t r = repeating.Inputs.NowUs;
		repeating.Inputs.Press(repeating.Space, r + 1 * Ms);
		repeating.FrameAt(r + 10 * Ms);
		RequirePhase(repeating.FrameAt(r + 120 * Ms), EActionPhase::Triggered, true, "Repeating hold, first");
		RequirePhase(repeating.FrameAt(r + 136 * Ms), EActionPhase::Triggered, true, "Repeating hold fires every frame");
	}

	void TestTap()
	{
		OneBinding test({ .Kind = ETriggerKind::Tap, .TapMaxSeconds = 0.2f });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 10 * Ms);
		RequirePhase(test.FrameAt(t + 20 * Ms), EActionPhase::Started, false, "Tap, press");
		test.Inputs.Release(test.Space, t + 110 * Ms);
		RequirePhase(test.FrameAt(t + 120 * Ms), EActionPhase::Triggered, true, "Tap, let go after 0.1 s");

		// Held too long
		test.Inputs.Press(test.Space, t + 200 * Ms);
		RequirePhase(test.FrameAt(t + 210 * Ms), EActionPhase::Started, false, "Tap, second press");
		RequirePhase(test.FrameAt(t + 410 * Ms), EActionPhase::Canceled, false, "Tap, held 0.21 s");
		RequirePhase(test.FrameAt(t + 426 * Ms), EActionPhase::Idle, false, "Tap, still held");
		test.Inputs.Release(test.Space, t + 430 * Ms);
		RequirePhase(test.FrameAt(t + 442 * Ms), EActionPhase::Idle, false, "Tap, let go too late: no tap");
	}

	void TestDoubleTap()
	{
		OneBinding test({ .Kind = ETriggerKind::DoubleTap, .DoubleTapMaxGapSeconds = 0.3f });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 10 * Ms);
		test.Inputs.Release(test.Space, t + 50 * Ms);
		RequirePhase(test.FrameAt(t + 60 * Ms), EActionPhase::Started, false, "DoubleTap, first tap");
		RequirePhase(test.FrameAt(t + 100 * Ms), EActionPhase::Ongoing, false, "DoubleTap, waiting");
		test.Inputs.Press(test.Space, t + 200 * Ms);
		RequirePhase(test.FrameAt(t + 210 * Ms), EActionPhase::Triggered, true, "DoubleTap, second press after 0.19 s");
		test.Inputs.Release(test.Space, t + 250 * Ms);

		test.Inputs.Press(test.Space, t + 400 * Ms);
		test.Inputs.Release(test.Space, t + 420 * Ms);
		RequirePhase(test.FrameAt(t + 430 * Ms), EActionPhase::Started, false, "DoubleTap, a lone tap");
		RequirePhase(test.FrameAt(t + 710 * Ms), EActionPhase::Canceled, false, "DoubleTap, no second tap within 0.3 s");
	}

	void TestRepeat()
	{
		OneBinding test({ .Kind = ETriggerKind::Repeat, .RepeatDelaySeconds = 0.4f, .RepeatIntervalSeconds = 0.1f });
		const uint64_t t = test.Inputs.NowUs;
		test.Inputs.Press(test.Space, t + 10 * Ms);
		RequirePhase(test.FrameAt(t + 20 * Ms), EActionPhase::Triggered, true, "Repeat, press");
		RequirePhase(test.FrameAt(t + 200 * Ms), EActionPhase::Triggered, false, "Repeat, before the delay");
		RequirePhase(test.FrameAt(t + 420 * Ms), EActionPhase::Triggered, true, "Repeat, after the delay");
		RequirePhase(test.FrameAt(t + 450 * Ms), EActionPhase::Triggered, false, "Repeat, before the interval");
		RequirePhase(test.FrameAt(t + 520 * Ms), EActionPhase::Triggered, true, "Repeat, after the interval");
		test.Inputs.Release(test.Space, t + 530 * Ms);
		RequirePhase(test.FrameAt(t + 540 * Ms), EActionPhase::Completed, false, "Repeat, let go");
	}

	void TestChord()
	{
		Rig rig;
		const ActionHandle modifier = rig.Input.AddAction({ "Modifier", EActionValue::Button });
		const ActionHandle sprint = rig.Input.AddAction({ "Sprint", EActionValue::Button });
		InputBinding sprintBinding = InputBinding::ForControl(sprint, Key(EKey::W), ETriggerKind::Chord);
		sprintBinding.Trigger.ChordAction = modifier;
		// The chord comes first in the context: it still sees this frame's Modifier
		rig.AddContext("OnFoot", 0, { sprintBinding, InputBinding::ForControl(modifier, Key(EKey::LeftShift)) });
		rig.Frame();

		rig.Press(Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Frame();
		RequirePhase(rig.Get(sprint), EActionPhase::Idle, false, "Chord without its action");

		rig.Press(Key(EKey::LeftShift), rig.NowUs + 1 * Ms);
		rig.Frame();
		RequirePhase(rig.Get(sprint), EActionPhase::Triggered, true, "Chord with its action");
	}

	void TestBuffer()
	{
		OneBinding buffered({ .Kind = ETriggerKind::Pressed, .BufferSeconds = 0.1f });
		const uint64_t t = buffered.Inputs.NowUs;
		buffered.Inputs.Press(buffered.Space, t + 5 * Ms);
		buffered.FrameAt(t + 16 * Ms);
		buffered.FrameAt(t + 32 * Ms);
		buffered.FrameAt(t + 48 * Ms);
		Require(buffered.Inputs.User().ConsumeTriggered(buffered.Action), "A buffered firing is still there 32 ms later");
		Require(!buffered.Inputs.User().ConsumeTriggered(buffered.Action), "And taken only once");

		buffered.Inputs.Release(buffered.Space, t + 50 * Ms);
		buffered.Inputs.Press(buffered.Space, t + 60 * Ms);
		buffered.FrameAt(t + 64 * Ms);
		buffered.FrameAt(t + 200 * Ms);
		Require(!buffered.Inputs.User().ConsumeTriggered(buffered.Action), "After the buffer it is gone");

		OneBinding plain({ .Kind = ETriggerKind::Pressed });
		const uint64_t p = plain.Inputs.NowUs;
		plain.Inputs.Press(plain.Space, p + 5 * Ms);
		plain.FrameAt(p + 16 * Ms);
		plain.FrameAt(p + 32 * Ms);
		Require(!plain.Inputs.User().ConsumeTriggered(plain.Action), "Without a buffer, a firing lasts its frame");
	}

	// An analog value resting near the threshold doesn't flicker
	void TestHysteresis()
	{
		Rig rig;
		const ActionHandle action = rig.Input.AddAction({ "Push", EActionValue::Button });
		InputBinding binding = InputBinding::ForControl(action, Mouse(EMouseControl::Delta));
		binding.SourceAxis = 0;
		rig.AddContext("Context", 0, { binding });
		rig.Frame();

		const auto frameWith = [&rig, action](float x)
		{
			rig.Move({ x, 0.0f }, rig.NowUs + 1 * Ms);
			rig.Frame();
			return rig.Get(action);
		};
		RequirePhase(frameWith(0.45f), EActionPhase::Idle, false, "Below the press threshold");
		RequirePhase(frameWith(0.55f), EActionPhase::Triggered, true, "Past the press threshold");
		RequirePhase(frameWith(0.45f), EActionPhase::Triggered, true, "Between the thresholds it stays down");
		RequirePhase(frameWith(0.35f), EActionPhase::Completed, false, "Below the release threshold");
	}

	// ---------------------------------------------------------------------------------------
	// Contexts

	void TestHigherContextTakes()
	{
		Rig rig;
		const ActionHandle jump = rig.Input.AddAction({ "Jump", EActionValue::Button });
		const ActionHandle confirm = rig.Input.AddAction({ "Confirm", EActionValue::Button });
		rig.AddContext("OnFoot", 0, { InputBinding::ForControl(jump, Key(EKey::Space), ETriggerKind::Pressed) });
		const ContextHandle menu = rig.AddContext("Menu", 100, { InputBinding::ForControl(confirm, Key(EKey::Space), ETriggerKind::Pressed) });
		rig.Frame();

		rig.Press(Key(EKey::Space), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Get(confirm).bTriggered, "The menu should get Space");
		Require(!rig.Get(jump).bTriggered, "Jump shouldn't, with the menu open");
		Require(rig.TraceOf(jump).Outcome == EBindingOutcome::Taken, "Jump's trace says why");
		Require(rig.TraceText(jump) == "Jump   OnFoot (0) - Keyboard/Space - Pressed\nstopped: taken by Menu (100)",
			"Jump's trace: " + rig.TraceText(jump));

		rig.User().SetContextActive(menu, false);
		rig.Release(Key(EKey::Space), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.TraceOf(confirm).Outcome == EBindingOutcome::ContextOff, "Confirm's context is off");
		rig.Press(Key(EKey::Space), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Get(jump).bTriggered, "With the menu closed, Space jumps");
	}

	void TestLettingThroughAndDevices()
	{
		Rig rig;
		const ActionHandle lower = rig.Input.AddAction({ "Lower", EActionValue::Button });
		const ActionHandle upper = rig.Input.AddAction({ "Upper", EActionValue::Button });
		InputBinding through = InputBinding::ForControl(upper, Key(EKey::E));
		through.bTakesControls = false;
		rig.AddContext("Lower", 0, { InputBinding::ForControl(lower, Key(EKey::E)) });
		rig.AddContext("Upper", 10, { through });
		rig.Frame();
		rig.Press(Key(EKey::E), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Get(upper).bTriggered && rig.Get(lower).bTriggered, "A binding that lets its control through leaves it to the context below");

		// A context that takes the whole keyboard, as the editor's source inspector does
		InputContextDef blocker{ .Name = "Blocker", .Priority = 950, .TakesDevices = DeviceBit(EDeviceKind::Keyboard) };
		const ContextHandle blockerHandle = rig.Input.AddContext(blocker);
		rig.User().SetContextActive(blockerHandle, true);
		rig.Frame();
		Require(!rig.Get(upper).bTriggered && !rig.Get(lower).bTriggered, "A context taking the keyboard takes every key");
		Require(rig.TraceText(lower).ends_with("stopped: taken by Blocker (950)"), "Lower's trace: " + rig.TraceText(lower));
	}

	void TestEqualPriorityLaterWins()
	{
		Rig rig;
		const ActionHandle first = rig.Input.AddAction({ "First", EActionValue::Button });
		const ActionHandle second = rig.Input.AddAction({ "Second", EActionValue::Button });
		rig.AddContext("First", 5, { InputBinding::ForControl(first, Key(EKey::F)) });
		rig.AddContext("Second", 5, { InputBinding::ForControl(second, Key(EKey::F)) });
		rig.Frame();
		rig.Press(Key(EKey::F), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Get(second).bTriggered && !rig.Get(first).bTriggered, "Of equal priorities, the context turned on later is on top");
		Require(rig.Input.GetContext(rig.User().GetStack().front().Context).Name == "Second", "The stack lists it first");
	}

	void TestCompositeAndPartialTaking()
	{
		Rig rig;
		const ActionHandle move = rig.Input.AddAction({ "Move", EActionValue::Axis3D });
		const ActionHandle interact = rig.Input.AddAction({ "Interact", EActionValue::Button });
		const ContextHandle onFoot = rig.AddContext("OnFoot", 0,
			{ InputBinding::ForComposite(move,
				{ .Right = Key(EKey::D), .Left = Key(EKey::A), .Forward = Key(EKey::W), .Back = Key(EKey::S) }) });
		const ContextHandle prompt = rig.AddContext("Prompt", 50, { InputBinding::ForControl(interact, Key(EKey::W)) });
		rig.Frame();

		rig.Press(Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Press(Key(EKey::D), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(Near(rig.Get(move).Vec3(), { 1.0f, 0.0f, 0.0f }), "W is taken by the prompt; D still moves right, at full length: " + Text(rig.Get(move).Vec3()));
		const BindingTrace& trace = rig.TraceOf(move);
		Require(trace.TakenControls.size() == 1 && trace.TakenControls[0] == Key(EKey::W), "The trace names the key that was taken");
		Require(DescribeBindingSource(rig.Input.GetContext(onFoot).Bindings[0]) == "Keyboard/D,A,W,S", "The composite's source text");

		rig.User().SetContextActive(prompt, false);
		rig.Frame();
		Require(Near(rig.Get(move).Vec3(), glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f))), "W and D together aren't faster than one of them");
	}

	void TestModifiers()
	{
		Rig rig;
		const ActionHandle crash = rig.Input.AddAction({ "Crash", EActionValue::Button });
		const ActionHandle copy = rig.Input.AddAction({ "Copy", EActionValue::Button });
		InputBinding chord = InputBinding::ForControl(crash, Key(EKey::C), ETriggerKind::Pressed);
		chord.Modifiers = EModifierKeys::Ctrl | EModifierKeys::Alt | EModifierKeys::Shift;
		rig.AddContext("Game", 0, { InputBinding::ForControl(copy, Key(EKey::C), ETriggerKind::Pressed) });
		rig.AddContext("Debug", 800, { chord });
		rig.Frame();

		rig.Press(Key(EKey::C), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(!rig.Get(crash).bTriggered, "C alone isn't the chord");
		Require(rig.Get(copy).bTriggered, "Without its modifiers, the chord doesn't take C");
		Require(rig.TraceText(crash).ends_with("stopped: Ctrl+Alt+Shift not held"), "Crash's trace: " + rig.TraceText(crash));
		rig.Release(Key(EKey::C), rig.NowUs + 1 * Ms);

		rig.Press(Key(EKey::RightControl), rig.NowUs + 1 * Ms);
		rig.Press(Key(EKey::LeftAlt), rig.NowUs + 2 * Ms);
		rig.Press(Key(EKey::LeftShift), rig.NowUs + 3 * Ms);
		rig.Press(Key(EKey::C), rig.NowUs + 4 * Ms);
		rig.Frame();
		Require(rig.Get(crash).bTriggered, "Ctrl+Alt+Shift+C, either Ctrl");
		Require(!rig.Get(copy).bTriggered, "With its modifiers held, the chord takes C");
	}

	void TestLargestValueWins()
	{
		Rig rig;
		const ActionHandle move = rig.Input.AddAction({ "Move", EActionValue::Axis2D });
		InputBinding slow = InputBinding::ForComposite(move, { .Right = Key(EKey::Right) });
		slow.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(0.5f) };
		rig.AddContext("OnFoot", 0, { slow, InputBinding::ForComposite(move, { .Right = Key(EKey::D) }) });
		rig.Frame();

		rig.Press(Key(EKey::Right), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(Near(rig.Get(move).Float(), 0.5f), "One binding: its value");
		rig.Press(Key(EKey::D), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(Near(rig.Get(move).Float(), 1.0f) && rig.Get(move).BindingIndex == 1, "Two bindings: the larger value, and which binding it was");
	}

	void TestZeroedTrace()
	{
		Rig rig;
		const ActionHandle look = rig.Input.AddAction({ "Look", EActionValue::Axis2D });
		InputBinding binding = InputBinding::ForControl(look, Mouse(EMouseControl::Delta));
		binding.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.5f, .Outer = 10.0f };
		rig.AddContext("Camera", 0, { binding });
		rig.Frame();
		rig.Move({ 0.3f, 0.0f }, rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.TraceOf(look).Outcome == EBindingOutcome::Zeroed && rig.TraceOf(look).ZeroedBy == EPipelineStep::DeadZone,
			"The dead zone zeroed it");
		Require(rig.TraceText(look) ==
				"Look   Camera (0) - Mouse/Delta - Down\nraw (0.3, 0, 0) -> dead zone (0, 0, 0) -> stopped: zeroed by dead zone",
			"Look's trace: " + rig.TraceText(look));
	}

	// ---------------------------------------------------------------------------------------
	// System contexts and the cursor

	void TestCursorFollowsStack()
	{
		Rig rig;
		Require(rig.Input.GetCursor().Mode == ECursorMode::Free, "Without a context saying otherwise, the cursor is free");

		int window = 0;
		const ContextHandle look = rig.Input.AddContext({ .Name = "Look", .Priority = 110, .Cursor = ECursorMode::Captured });
		const ContextHandle hider = rig.Input.AddContext({ .Name = "Hider", .Priority = 50, .Cursor = ECursorMode::Hidden });
		rig.User().SetContextActive(hider, true);
		rig.User().SetContextActive(look, true, { .Window = &window });
		rig.Frame();
		Require(rig.Input.GetCursor().Mode == ECursorMode::Captured && rig.Input.GetCursor().Owner == "Look",
			"The topmost context that says something decides");
		Require(rig.Backend.GetAppliedCursor().Window == &window, "The cursor is captured in the context's window");

		rig.User().SetContextActive(look, false);
		rig.Frame();
		Require(rig.Input.GetCursor().Mode == ECursorMode::Hidden && rig.Input.GetCursor().Owner == "Hider", "Then the next one down");

		// Losing the focus frees it
		rig.User().SetContextActive(look, true);
		rig.Backend.SetFocus(false, rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.User().IsContextActive(rig.Input.GetUnfocusedContext()), "Unfocused is on");
		Require(rig.Input.GetCursor().Mode == ECursorMode::Free && rig.Input.GetCursor().Owner == "Unfocused", "Without the focus, the cursor is free");
		Require(rig.User().GetStack().size() == 3, "Unfocused, Look and Hider on the stack");

		rig.Backend.SetFocus(true, rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Input.GetCursor().Mode == ECursorMode::Captured, "With the focus back, captured again");
	}

	void TestImGuiContext()
	{
		Rig rig;
		const ActionHandle move = rig.Input.AddAction({ "Move", EActionValue::Button });
		const ActionHandle click = rig.Input.AddAction({ "Click", EActionValue::Button });
		rig.AddContext("Game", 0,
			{ InputBinding::ForControl(move, Key(EKey::W)), InputBinding::ForControl(click, Mouse(EMouseControl::Left)) });
		rig.Frame();

		rig.Input.SetImGuiState({ .bTypingText = true });
		rig.Press(Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Press(Mouse(EMouseControl::Left), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(!rig.Get(move).bTriggered && rig.Get(click).bTriggered, "While typing, ImGui takes the keys, not the mouse");
		Require(rig.TraceText(move).ends_with("stopped: taken by ImGui (900)"), "Move's trace: " + rig.TraceText(move));

		rig.Input.SetImGuiState({ .bMouseOverUI = true });
		rig.Frame();
		Require(rig.Get(move).bTriggered && !rig.Get(click).bTriggered, "Over ImGui's UI, ImGui takes the mouse, not the keys");

		// Not while something holds the cursor captured: it wanders over ImGui's windows unseen
		const ContextHandle look = rig.Input.AddContext({ .Name = "Look", .Priority = 110, .Cursor = ECursorMode::Captured });
		rig.User().SetContextActive(look, true);
		rig.Frame();
		rig.Frame();
		Require(rig.Get(click).bTriggered, "With the cursor captured, ImGui doesn't take the mouse");
	}

	void TestLastUsedDevice()
	{
		Rig rig;
		rig.Press(Key(EKey::W), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == rig.Keyboard, "A key press: the keyboard");

		rig.Move({ 1.0f, 1.0f }, rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == rig.Keyboard, "A nudged mouse doesn't count");

		rig.Move({ 2.0f, 0.0f }, rig.NowUs + 1 * Ms);
		rig.Move({ 2.0f, 0.0f }, rig.NowUs + 2 * Ms);
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == rig.Mouse, "A mouse moved a few pixels does");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestDeadZones();
		TestCurves();
		TestPipelineSteps();
		TestDown();
		TestPressedAndReleased();
		TestToggle();
		TestHold();
		TestTap();
		TestDoubleTap();
		TestRepeat();
		TestChord();
		TestBuffer();
		TestHysteresis();
		TestHigherContextTakes();
		TestLettingThroughAndDevices();
		TestEqualPriorityLaterWins();
		TestCompositeAndPartialTaking();
		TestModifiers();
		TestLargestValueWins();
		TestZeroedTrace();
		TestCursorFollowsStack();
		TestImGuiContext();
		TestLastUsedDevice();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input router test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input router tests passed.\n";
	return 0;
}

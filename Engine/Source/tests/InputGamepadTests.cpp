// Gamepads without a pad: virtual pads through the router and the fly camera's bindings, output
// (rumble, trigger effects, light) merged, checked against what each pad can do and recorded, and
// the DualSense's trigger effect bytes against the layout SDL uses.
#include "FlyCameraInput.h"
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

	constexpr uint64_t Ms = 1000;

	// An input system with a virtual pad, and frames 16 ms apart
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;

		Rig()
		{
			Input.AddBackend(Backend);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			Frame();
		}

		InputUser& User()
		{
			return Input.GetUser();
		}

		void SetPad(EGamepadModel model, EDeviceCapability capabilities)
		{
			InputDeviceInfo info;
			info.Name = "Pad";
			info.Model = model;
			info.Capabilities = capabilities;
			Backend.SetDeviceInfo(Pad, info, NowUs + 1);
		}

		void Set(EGamepadControl control, glm::vec3 value)
		{
			Backend.SetValue(Pad, Gamepad(control), value, NowUs + 1 * Ms);
		}

		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			Input.EndFrame();
		}

		const BindingTrace& TraceOf(ActionHandle action)
		{
			const std::vector<const BindingTrace*> traces = User().GetTraces(action);
			Require(traces.size() == 1, "Expected one binding for " + Input.GetAction(action).Name);
			return *traces.front();
		}
	};

	// ---------------------------------------------------------------------------------------
	// Routing

	void TestUnfocusedTakesPads()
	{
		Rig rig;
		const ActionHandle jump = rig.Input.AddAction({ "Jump" });
		const ContextHandle game = rig.Input.AddContext({ .Name = "Game", .Bindings = { InputBinding::ForControl(jump, Gamepad(EGamepadControl::FaceSouth)) } });
		rig.User().SetContextActive(game, true);
		rig.Backend.SetFocus(false, rig.NowUs + 1);
		rig.Frame();

		rig.Backend.Press(rig.Pad, Gamepad(EGamepadControl::FaceSouth), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Input.GetFrame().IsDown(Gamepad(EGamepadControl::FaceSouth)), "The frame still has the pad's button");
		Require(!rig.User().Get(jump).bTriggered, "But without the focus, Jump doesn't fire");
		Require(FormatTrace(rig.Input, rig.TraceOf(jump)).ends_with("stopped: taken by Unfocused (1000)"), "The trace says so");
	}

	void TestSwizzleAndAnalogComposite()
	{
		Rig rig;
		const ActionHandle move = rig.Input.AddAction({ "Move", EActionValue::Axis3D });
		const ActionHandle vertical = rig.Input.AddAction({ "Vertical", EActionValue::Axis1D });
		InputBinding stick = InputBinding::ForControl(move, Gamepad(EGamepadControl::LeftStick));
		stick.Swizzle = glm::ivec3(0, -1, 1);
		const InputBinding triggers = InputBinding::ForComposite(vertical,
			{ .Right = Gamepad(EGamepadControl::RightTrigger), .Left = Gamepad(EGamepadControl::LeftTrigger) });
		const ContextHandle context = rig.Input.AddContext({ .Name = "Fly", .Bindings = { stick, triggers } });
		rig.User().SetContextActive(context, true);
		rig.Frame();

		rig.Set(EGamepadControl::LeftStick, { 0.3f, 0.8f, 0.0f });
		rig.Set(EGamepadControl::RightTrigger, { 0.6f, 0.0f, 0.0f });
		rig.Set(EGamepadControl::LeftTrigger, { 0.2f, 0.0f, 0.0f });
		rig.Frame();
		Require(Near(rig.User().Get(move).Vec3(), { 0.3f, 0.0f, 0.8f }), "The stick's up becomes forward (z)");
		Require(Near(rig.User().Get(vertical).Float(), 0.4f), "Triggers in a composite add their values: 0.6 up, 0.2 down");
		Require(DescribeBindingSource(stick) == "Gamepad/LeftStick (x, 0, y)", "The swizzle in the source text: " + DescribeBindingSource(stick));
		Require(rig.TraceOf(move).Device == rig.Pad, "The trace names the pad");
	}

	void TestLastUsedPad()
	{
		Rig rig;
		VirtualInputBackend& backend = rig.Backend;
		const InputDeviceId keyboard = backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", rig.NowUs + 1);
		backend.Press(keyboard, Key(EKey::W), rig.NowUs + 2);
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == keyboard, "The keyboard first");

		rig.Set(EGamepadControl::LeftStick, { 0.1f, 0.15f, 0.0f });
		rig.Set(EGamepadControl::Gyro, { 2.0f, 0.0f, 0.0f });
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == keyboard, "A drifting stick or the gyro doesn't count");

		rig.Set(EGamepadControl::LeftStick, { 0.5f, 0.0f, 0.0f });
		rig.Frame();
		Require(rig.User().GetLastUsedDevice() == rig.Pad, "A stick pushed past its rest does");
	}

	// ---------------------------------------------------------------------------------------
	// Output

	void TestOutputMergedAndScaled()
	{
		Rig rig;
		rig.SetPad(EGamepadModel::XboxOne, EDeviceCapability::Rumble | EDeviceCapability::TriggerRumble);
		rig.Frame();

		rig.User().SetHapticsStrength(0.5f);
		rig.User().Rumble({ .Low = 0.8f, .High = 0.1f, .DurationMs = 100 });
		rig.User().Rumble({ .Low = 0.2f, .High = 0.6f, .DurationMs = 300 });
		rig.Frame();

		const auto& outputs = rig.Backend.GetOutputs();
		Require(outputs.size() == 1 && outputs[0].first == rig.Pad, "One output for the pad, for the whole frame");
		const RumbleOutput& rumble = *outputs[0].second.Rumble;
		Require(Near(rumble.Low, 0.4f) && Near(rumble.High, 0.3f) && rumble.DurationMs == 300,
			"The stronger of each motor, the longer time, at half strength");
		Require(rig.User().GetOutputRecords().size() == 1 && rig.User().GetOutputRecords()[0].bSent, "The record says it was sent");

		rig.Frame();
		Require(rig.Backend.GetOutputs().size() == 1 && rig.User().GetOutputRecords().empty(), "Nothing asked, nothing sent");
	}

	void TestOutputOnlyWhatPadsCan()
	{
		Rig rig;
		rig.SetPad(EGamepadModel::XboxOne, EDeviceCapability::Rumble | EDeviceCapability::TriggerRumble);
		rig.Frame();

		const TriggerEffect weapon{ .Kind = ETriggerEffectKind::Weapon, .Start = 0.3f, .End = 0.6f, .Strength = 0.8f };
		rig.User().SetTriggerEffect(ETrigger::Right, weapon);
		rig.User().SetLightColor({ 1.0f, 0.0f, 0.0f });
		rig.Frame();
		Require(rig.Backend.GetOutputs().empty(), "An Xbox pad gets neither trigger effects nor a light colour");
		const auto& records = rig.User().GetOutputRecords();
		Require(records.size() == 2 && !records[0].bSent && records[0].Reason == "Xbox One has no adaptive triggers",
			"The record says why: " + (records.empty() ? std::string() : records[0].Reason));
		Require(records[0].What == "Trigger effect right: Weapon 0.3 to 0.6, strength 0.8", "And what: " + records[0].What);
		Require(records[1].Reason == "Xbox One has no light", "And for the light: " + records[1].Reason);

		rig.SetPad(EGamepadModel::DualSense, EDeviceCapability::Rumble | EDeviceCapability::AdaptiveTriggers | EDeviceCapability::Lightbar);
		rig.Frame();
		rig.User().SetTriggerEffect(ETrigger::Right, weapon);
		rig.User().TriggerRumble({ .Left = 1.0f, .DurationMs = 50 });
		rig.Frame();
		Require(rig.Backend.GetOutputs().size() == 1, "The DualSense gets the effect");
		const DeviceOutput& sent = rig.Backend.GetOutputs()[0].second;
		Require(sent.RightTriggerEffect && !sent.LeftTriggerEffect && !sent.TriggerRumble, "Only what it can do: here, no trigger rumble");

		rig.Backend.RemoveDevice(rig.Pad, rig.NowUs + 1);
		rig.Frame();
		rig.User().Rumble({ .Low = 1.0f, .DurationMs = 50 });
		rig.Frame();
		Require(rig.User().GetOutputRecords().size() == 1 && rig.User().GetOutputRecords()[0].Reason == "No gamepad connected",
			"Without a pad, the record says so");
	}

	void TestDualSenseTriggerBytes()
	{
		using Bytes = std::array<uint8_t, 11>;
		Require(EncodeDualSenseTriggerEffect({ .Kind = ETriggerEffectKind::Off }) == Bytes{ 0x05 }, "Off");
		Require(EncodeDualSenseTriggerEffect({ .Kind = ETriggerEffectKind::Resistance, .Start = 0.0f, .Strength = 110.0f / 255.0f }) ==
				Bytes{ 0x01, 0, 110 },
			"Resistance, as SDL's testcontroller sends it");
		Require(EncodeDualSenseTriggerEffect({ .Kind = ETriggerEffectKind::Vibration, .Start = 128.0f / 255.0f, .Strength = 63.0f / 255.0f, .FrequencyHz = 15.0f }) == Bytes{ 0x06, 15, 63, 128 },
			"Vibration, as SDL's testcontroller sends it");
		Require(EncodeDualSenseTriggerEffect({ .Kind = ETriggerEffectKind::Weapon, .Start = 0.3f, .End = 0.6f, .Strength = 0.8f }) ==
				Bytes{ 0x02, 77, 153, 204 },
			"Weapon: start, end, strength");

		const TriggerEffect off{ .Kind = ETriggerEffectKind::Off };
		const std::array<uint8_t, 47> both = MakeDualSenseTriggerReport(off, off);
		Require(both[0] == 0x0C && both[10] == 0x05 && both[21] == 0x05, "Both triggers: both enable bits, right at 10, left at 21");
		const std::array<uint8_t, 47> right = MakeDualSenseTriggerReport(std::nullopt, off);
		Require(right[0] == 0x04 && right[10] == 0x05 && right[21] == 0x00, "Only the right one: the left one stays as it is");
	}

	// ---------------------------------------------------------------------------------------
	// The fly camera's bindings

	void TestFlyCameraWithPad()
	{
		Rig rig;
		FlyCameraInput fly(rig.Input, {});
		fly.SetActive(true, false, "Test");
		rig.Frame();

		rig.Set(EGamepadControl::LeftStick, { 0.0f, 1.0f, 0.0f });
		rig.Set(EGamepadControl::RightTrigger, { 1.0f, 0.0f, 0.0f });
		rig.Set(EGamepadControl::RightStick, { 1.0f, 0.0f, 0.0f });
		rig.Frame();

		const FlyCameraInput::Values values = fly.Read();
		Require(Near(values.Move, { 0.0f, 1.0f, 1.0f }), "Left stick up flies forward, the right trigger up");
		Require(Near(values.Look.x, fly.GetSettings().StickTurnRate * 0.016f) && Near(values.Look.y, 0.0f),
			"The right stick turns at its rate times the frame's time");

		// A drifting stick does nothing
		rig.Set(EGamepadControl::LeftStick, { 0.05f, 0.1f, 0.0f });
		rig.Set(EGamepadControl::RightTrigger, { 0.0f, 0.0f, 0.0f });
		rig.Set(EGamepadControl::RightStick, { 0.0f, 0.0f, 0.0f });
		rig.Frame();
		Require(Near(fly.Read().Move, glm::vec3(0.0f)), "Inside the dead zone, no movement");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestUnfocusedTakesPads();
		TestSwizzleAndAnalogComposite();
		TestLastUsedPad();
		TestOutputMergedAndScaled();
		TestOutputOnlyWhatPadsCan();
		TestDualSenseTriggerBytes();
		TestFlyCameraWithPad();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input gamepad test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input gamepad tests passed.\n";
	return 0;
}

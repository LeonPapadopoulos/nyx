// NyxPadCheck.exe: checks gamepads by hand, without the editor or a window. Lists every pad SDL
// finds, prints what it reports, and tries what it can do on button presses:
//   South (A / Cross)    rumble
//   East (B / Circle)    trigger rumble
//   West (X / Square)    the next DualSense trigger effect
//   North (Y / Triangle) the next light colour
//   Start + Back         quit
// It runs in a console, which is no window of SDL's: pads reporting here also report while
// another program's window has the focus.
//
//   NyxPadCheck [--seconds <n>] [--sensors]
// --seconds ends it after n seconds; --sensors prints gyro and accelerometer too.
#include "InputSystem.h"
#include "Log.h"
#include "SdlGamepadBackend.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx::Input;

	void PrintDevice(const InputDeviceInfo& device)
	{
		std::cout << "  " << device.Name << ": " << GetGamepadModelName(device.Model) << ", " << GetConnectionName(device.Connection)
				  << ", battery " << (device.BatteryPercent ? std::to_string(*device.BatteryPercent) + "%" : std::string("unknown"))
				  << ", key \"" << device.StableKey << "\"\n    can:";
		const std::array<std::pair<EDeviceCapability, const char*>, 8> capabilities = { {
			{ EDeviceCapability::Rumble, "rumble" },
			{ EDeviceCapability::TriggerRumble, "trigger rumble" },
			{ EDeviceCapability::AdaptiveTriggers, "adaptive triggers" },
			{ EDeviceCapability::Gyro, "gyro" },
			{ EDeviceCapability::Accelerometer, "accelerometer" },
			{ EDeviceCapability::Touchpad, "touchpad" },
			{ EDeviceCapability::Lightbar, "light" },
			{ EDeviceCapability::PlayerLeds, "player LEDs" },
		} };
		for (const auto& [capability, name] : capabilities)
		{
			if (HasCapability(device.Capabilities, capability))
			{
				std::cout << " " << name;
			}
		}
		std::cout << "\n";
	}

	std::string FormatControl(const InputEvent& event)
	{
		char value[96];
		if (IsButton(event.Control))
		{
			std::snprintf(value, sizeof(value), "%s", event.Value.x != 0.0f ? "down" : "up");
		}
		else
		{
			std::snprintf(value, sizeof(value), "(%.3f, %.3f, %.3f)", event.Value.x, event.Value.y, event.Value.z);
		}
		return GetControlPath(event.Control) + " " + value;
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();

	double seconds = 0.0;
	bool bSensors = false;
	for (int index = 1; index < argc; ++index)
	{
		if (std::strcmp(argv[index], "--seconds") == 0 && index + 1 < argc)
		{
			seconds = std::atof(argv[++index]);
		}
		else if (std::strcmp(argv[index], "--sensors") == 0)
		{
			bSensors = true;
		}
	}

	SdlGamepadBackend gamepads;
	if (!gamepads.IsAvailable())
	{
		std::cerr << "SDL's gamepad subsystem didn't start; see the log above.\n";
		return 1;
	}

	InputSystem input;
	input.AddBackend(gamepads);

	const ActionHandle rumble = input.AddAction({ "Rumble" });
	const ActionHandle triggerRumble = input.AddAction({ "TriggerRumble" });
	const ActionHandle triggerEffect = input.AddAction({ "TriggerEffect" });
	const ActionHandle light = input.AddAction({ "Light" });
	const ActionHandle quit = input.AddAction({ "Quit" });
	InputBinding quitBinding = InputBinding::ForControl(quit, Gamepad(EGamepadControl::Start), ETriggerKind::Chord);
	const ActionHandle back = input.AddAction({ "Back" });
	quitBinding.Trigger.ChordAction = back;
	const ContextHandle check = input.AddContext({
		.Name = "PadCheck",
		.Bindings = {
			InputBinding::ForControl(rumble, Gamepad(EGamepadControl::FaceSouth), ETriggerKind::Pressed),
			InputBinding::ForControl(triggerRumble, Gamepad(EGamepadControl::FaceEast), ETriggerKind::Pressed),
			InputBinding::ForControl(triggerEffect, Gamepad(EGamepadControl::FaceWest), ETriggerKind::Pressed),
			InputBinding::ForControl(light, Gamepad(EGamepadControl::FaceNorth), ETriggerKind::Pressed),
			InputBinding::ForControl(back, Gamepad(EGamepadControl::Back)),
			quitBinding,
		},
	});
	input.GetUser().SetContextActive(check, true, { .Reason = "NyxPadCheck" });

	const std::array<TriggerEffect, 4> effects = { {
		{ .Kind = ETriggerEffectKind::Resistance, .Start = 0.0f, .Strength = 0.5f },
		{ .Kind = ETriggerEffectKind::Weapon, .Start = 0.3f, .End = 0.6f, .Strength = 0.8f },
		{ .Kind = ETriggerEffectKind::Vibration, .Start = 0.5f, .Strength = 0.25f, .FrequencyHz = 15.0f },
		{ .Kind = ETriggerEffectKind::Off },
	} };
	const std::array<glm::vec3, 4> colors = { { { 1.0f, 0.2f, 0.1f }, { 0.1f, 1.0f, 0.2f }, { 0.1f, 0.3f, 1.0f }, { 1.0f, 1.0f, 1.0f } } };
	size_t nextEffect = 0;
	size_t nextColor = 0;

	std::cout << "Press South to rumble, East for trigger rumble, West for the next trigger effect, North for the next light colour,"
				 " Start + Back to quit.\n";

	const auto start = std::chrono::steady_clock::now();
	while (seconds <= 0.0 || std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds)
	{
		input.BeginFrame(NowUs());
		const InputFrame& frame = input.GetFrame();
		InputUser& user = input.GetUser();

		for (const InputEvent& event : frame.GetEvents())
		{
			if (event.Kind == EInputEventKind::DeviceAdded)
			{
				PrintDevice(event.Device);
			}
			else if (event.Kind == EInputEventKind::DeviceChanged)
			{
				std::cout << "Changed:\n";
				PrintDevice(event.Device);
			}
			else if (event.Kind == EInputEventKind::Control)
			{
				const bool bSensor = event.Control == Gamepad(EGamepadControl::Gyro) || event.Control == Gamepad(EGamepadControl::Accel);
				if (!bSensor || bSensors)
				{
					std::cout << FormatControl(event) << "\n";
				}
			}
		}

		if (user.ConsumeTriggered(quit))
		{
			break;
		}
		if (user.ConsumeTriggered(rumble))
		{
			user.Rumble({ .Low = 0.6f, .High = 0.3f, .DurationMs = 250 });
		}
		if (user.ConsumeTriggered(triggerRumble))
		{
			user.TriggerRumble({ .Left = 0.5f, .Right = 0.5f, .DurationMs = 250 });
		}
		if (user.ConsumeTriggered(triggerEffect))
		{
			user.SetTriggerEffect(ETrigger::Left, effects[nextEffect]);
			user.SetTriggerEffect(ETrigger::Right, effects[nextEffect]);
			nextEffect = (nextEffect + 1) % effects.size();
		}
		if (user.ConsumeTriggered(light))
		{
			user.SetLightColor(colors[nextColor]);
			nextColor = (nextColor + 1) % colors.size();
		}

		input.EndFrame();
		for (const InputUser::OutputRecord& record : user.GetOutputRecords())
		{
			std::cout << (record.bSent ? "Sent: " : "Not sent: ") << record.What << (record.Reason.empty() ? "" : " (" + record.Reason + ")") << "\n";
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	std::cout << "Gamepads seen:\n";
	for (const InputFrame::DeviceState& device : input.GetFrame().GetDevices())
	{
		PrintDevice(device.Info);
	}
	return 0;
}

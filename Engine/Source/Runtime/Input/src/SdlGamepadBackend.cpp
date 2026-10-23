#include "SdlGamepadBackend.h"

#include "Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>

namespace Nyx::Input
{
	namespace
	{
		constexpr uint16_t SonyVendor = 0x054C;
		constexpr uint16_t DualSenseEdgeProduct = 0x0DF2;

		// SDL's buttons up to the touchpad are in the order of EGamepadControl's
		static_assert(SDL_GAMEPAD_BUTTON_SOUTH == 0 && SDL_GAMEPAD_BUTTON_TOUCHPAD == static_cast<int>(EGamepadControl::TouchpadButton),
			"EGamepadControl's buttons follow SDL_GamepadButton");

		float StickAxis(int16_t value)
		{
			return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
		}

		float TriggerAxis(int16_t value)
		{
			return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
		}

		uint16_t Motor(float strength)
		{
			return static_cast<uint16_t>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
		}

		uint8_t Channel(float value)
		{
			return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		EGamepadModel ModelOf(SDL_Gamepad* gamepad)
		{
			switch (SDL_GetGamepadType(gamepad))
			{
			case SDL_GAMEPAD_TYPE_XBOX360: return EGamepadModel::Xbox360;
			case SDL_GAMEPAD_TYPE_XBOXONE: return EGamepadModel::XboxOne;
			case SDL_GAMEPAD_TYPE_PS4:     return EGamepadModel::DualShock4;
			case SDL_GAMEPAD_TYPE_PS5:
				return SDL_GetGamepadVendor(gamepad) == SonyVendor && SDL_GetGamepadProduct(gamepad) == DualSenseEdgeProduct
					? EGamepadModel::DualSenseEdge
					: EGamepadModel::DualSense;
			default: return EGamepadModel::Generic;
			}
		}

		std::optional<int> BatteryOf(SDL_Gamepad* gamepad)
		{
			int percent = -1;
			const SDL_PowerState state = SDL_GetGamepadPowerInfo(gamepad, &percent);
			if ((state == SDL_POWERSTATE_ON_BATTERY || state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED) && percent >= 0)
			{
				return percent;
			}
			return std::nullopt;
		}

		InputEvent ControlEvent(InputDeviceId device, ControlId control, glm::vec3 value, uint64_t timeUs)
		{
			InputEvent event;
			event.Kind = EInputEventKind::Control;
			event.TimeUs = timeUs;
			event.Device.Id = device;
			event.Control = control;
			event.Value = value;
			return event;
		}
	}

	SdlGamepadBackend::SdlGamepadBackend()
	{
		// Nyx decides about focus itself (the Unfocused context); SDL has no window to ask anyway
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

		if (!SDL_Init(SDL_INIT_GAMEPAD))
		{
			LOG_WARNING("Input: no gamepads, SDL couldn't start its gamepad subsystem: {0}", SDL_GetError());
			return;
		}

		bAvailable = true;
		ClockOffsetUs = static_cast<int64_t>(NowUs()) - static_cast<int64_t>(SDL_GetTicksNS() / 1000);
		LOG_INFO("Input: gamepads through SDL {0}.{1}.{2}", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
			SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
	}

	SdlGamepadBackend::~SdlGamepadBackend()
	{
		for (Pad& pad : Pads)
		{
			SDL_CloseGamepad(pad.Handle);
		}
		Pads.clear();

		if (bAvailable)
		{
			SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
			SDL_Quit();
		}
	}

	uint64_t SdlGamepadBackend::ToNowUs(uint64_t sdlNanoseconds) const
	{
		const int64_t timeUs = static_cast<int64_t>(sdlNanoseconds / 1000) + ClockOffsetUs;
		return static_cast<uint64_t>(std::max<int64_t>(timeUs, 0));
	}

	SdlGamepadBackend::Pad* SdlGamepadBackend::FindPad(uint32_t sdlId)
	{
		const auto found = std::find_if(Pads.begin(), Pads.end(),
			[sdlId](const Pad& pad)
			{
				return pad.SdlId == sdlId;
			});
		return found != Pads.end() ? &*found : nullptr;
	}

	SdlGamepadBackend::Pad* SdlGamepadBackend::FindPad(InputDeviceId device)
	{
		const auto found = std::find_if(Pads.begin(), Pads.end(),
			[device](const Pad& pad)
			{
				return pad.Info.Id == device;
			});
		return found != Pads.end() ? &*found : nullptr;
	}

	void SdlGamepadBackend::OpenPad(uint32_t sdlId, uint64_t timeUs, std::vector<InputEvent>& outEvents)
	{
		if (FindPad(sdlId))
		{
			return;
		}

		SDL_Gamepad* gamepad = SDL_OpenGamepad(sdlId);
		if (!gamepad)
		{
			LOG_WARNING("Input: couldn't open gamepad {0}: {1}", sdlId, SDL_GetError());
			return;
		}

		Pad pad;
		pad.Handle = gamepad;
		pad.SdlId = sdlId;

		InputDeviceInfo& info = pad.Info;
		info.Id = MakeDeviceId();
		info.Kind = EDeviceKind::Gamepad;
		const char* name = SDL_GetGamepadName(gamepad);
		info.Name = name ? name : "Gamepad";
		info.Backend = GetName();
		info.Model = ModelOf(gamepad);
		info.BatteryPercent = BatteryOf(gamepad);

		switch (SDL_GetGamepadConnectionState(gamepad))
		{
		case SDL_JOYSTICK_CONNECTION_WIRED:    info.Connection = EConnection::Wired; break;
		case SDL_JOYSTICK_CONNECTION_WIRELESS: info.Connection = EConnection::Wireless; break;
		default:                               info.Connection = EConnection::Unknown; break;
		}

		const char* serial = SDL_GetGamepadSerial(gamepad);
		const char* path = SDL_GetGamepadPath(gamepad);
		info.StableKey = serial && *serial ? serial : path ? path
														   : "";

		const SDL_PropertiesID properties = SDL_GetGamepadProperties(gamepad);
		const auto has = [properties](const char* property)
		{
			return SDL_GetBooleanProperty(properties, property, false);
		};
		EDeviceCapability capabilities = EDeviceCapability::None;
		capabilities = has(SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN) ? capabilities | EDeviceCapability::Rumble : capabilities;
		capabilities = has(SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN) ? capabilities | EDeviceCapability::TriggerRumble : capabilities;
		capabilities = has(SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN) ? capabilities | EDeviceCapability::Lightbar : capabilities;
		capabilities = has(SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN) ? capabilities | EDeviceCapability::PlayerLeds : capabilities;
		capabilities = SDL_GetNumGamepadTouchpads(gamepad) > 0 ? capabilities | EDeviceCapability::Touchpad : capabilities;
		if (info.Model == EGamepadModel::DualSense || info.Model == EGamepadModel::DualSenseEdge)
		{
			capabilities = capabilities | EDeviceCapability::AdaptiveTriggers;
		}

		// Sensors only report once turned on; on a DualShock or DualSense over Bluetooth, that also
		// switches it to the reports that carry rumble and the light
		if (SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true))
		{
			capabilities = capabilities | EDeviceCapability::Gyro;
		}
		if (SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true))
		{
			capabilities = capabilities | EDeviceCapability::Accelerometer;
		}
		info.Capabilities = capabilities;

		InputEvent added;
		added.Kind = EInputEventKind::DeviceAdded;
		added.TimeUs = timeUs;
		added.Device = info;
		outEvents.push_back(std::move(added));

		// What is held already: SDL only reports changes
		for (int button = 0; button <= SDL_GAMEPAD_BUTTON_TOUCHPAD; ++button)
		{
			if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)))
			{
				outEvents.push_back(ControlEvent(info.Id, Gamepad(static_cast<EGamepadControl>(button)), glm::vec3(1.0f, 0.0f, 0.0f), timeUs));
			}
		}
		pad.LeftStick = glm::vec3(StickAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)),
			-StickAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)), 0.0f);
		pad.RightStick = glm::vec3(StickAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX)),
			-StickAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)), 0.0f);
		outEvents.push_back(ControlEvent(info.Id, Gamepad(EGamepadControl::LeftStick), pad.LeftStick, timeUs));
		outEvents.push_back(ControlEvent(info.Id, Gamepad(EGamepadControl::RightStick), pad.RightStick, timeUs));
		outEvents.push_back(ControlEvent(info.Id, Gamepad(EGamepadControl::LeftTrigger),
			glm::vec3(TriggerAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)), 0.0f, 0.0f), timeUs));
		outEvents.push_back(ControlEvent(info.Id, Gamepad(EGamepadControl::RightTrigger),
			glm::vec3(TriggerAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)), 0.0f, 0.0f), timeUs));

		Pads.push_back(std::move(pad));
	}

	void SdlGamepadBackend::ClosePad(uint32_t sdlId, uint64_t timeUs, std::vector<InputEvent>& outEvents)
	{
		Pad* pad = FindPad(sdlId);
		if (!pad)
		{
			return;
		}

		InputEvent removed;
		removed.Kind = EInputEventKind::DeviceRemoved;
		removed.TimeUs = timeUs;
		removed.Device.Id = pad->Info.Id;
		outEvents.push_back(std::move(removed));

		SDL_CloseGamepad(pad->Handle);
		std::erase_if(Pads,
			[sdlId](const Pad& candidate)
			{
				return candidate.SdlId == sdlId;
			});
	}

	void SdlGamepadBackend::Poll(std::vector<InputEvent>& outEvents)
	{
		if (!bAvailable)
		{
			return;
		}

		// Updates SDL's devices and queues their events. SDL's own hidden windows get their
		// messages from GLFW's PollEvents, which runs before, on the same thread.
		SDL_PumpEvents();

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			const uint64_t timeUs = ToNowUs(event.common.timestamp);
			switch (event.type)
			{
			case SDL_EVENT_GAMEPAD_ADDED:
				OpenPad(event.gdevice.which, timeUs, outEvents);
				break;

			case SDL_EVENT_GAMEPAD_REMOVED:
				ClosePad(event.gdevice.which, timeUs, outEvents);
				break;

			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
				if (Pad* pad = FindPad(event.gbutton.which); pad && event.gbutton.button <= SDL_GAMEPAD_BUTTON_TOUCHPAD)
				{
					outEvents.push_back(ControlEvent(pad->Info.Id, Gamepad(static_cast<EGamepadControl>(event.gbutton.button)),
						glm::vec3(event.gbutton.down ? 1.0f : 0.0f, 0.0f, 0.0f), timeUs));
				}
				break;

			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				if (Pad* pad = FindPad(event.gaxis.which))
				{
					// Sticks: up is positive, SDL's y points down
					const int16_t value = event.gaxis.value;
					switch (event.gaxis.axis)
					{
					case SDL_GAMEPAD_AXIS_LEFTX:  pad->LeftStick.x = StickAxis(value); break;
					case SDL_GAMEPAD_AXIS_LEFTY:  pad->LeftStick.y = -StickAxis(value); break;
					case SDL_GAMEPAD_AXIS_RIGHTX: pad->RightStick.x = StickAxis(value); break;
					case SDL_GAMEPAD_AXIS_RIGHTY: pad->RightStick.y = -StickAxis(value); break;
					default:                      break;
					}

					switch (event.gaxis.axis)
					{
					case SDL_GAMEPAD_AXIS_LEFTX:
					case SDL_GAMEPAD_AXIS_LEFTY:
						outEvents.push_back(ControlEvent(pad->Info.Id, Gamepad(EGamepadControl::LeftStick), pad->LeftStick, timeUs));
						break;
					case SDL_GAMEPAD_AXIS_RIGHTX:
					case SDL_GAMEPAD_AXIS_RIGHTY:
						outEvents.push_back(ControlEvent(pad->Info.Id, Gamepad(EGamepadControl::RightStick), pad->RightStick, timeUs));
						break;
					case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
						outEvents.push_back(
							ControlEvent(pad->Info.Id, Gamepad(EGamepadControl::LeftTrigger), glm::vec3(TriggerAxis(value), 0.0f, 0.0f), timeUs));
						break;
					case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
						outEvents.push_back(
							ControlEvent(pad->Info.Id, Gamepad(EGamepadControl::RightTrigger), glm::vec3(TriggerAxis(value), 0.0f, 0.0f), timeUs));
						break;
					default:
						break;
					}
				}
				break;

			case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
			case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
			case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
				if (Pad* pad = FindPad(event.gtouchpad.which); pad && event.gtouchpad.touchpad == 0 && event.gtouchpad.finger < 2)
				{
					const EGamepadControl finger = event.gtouchpad.finger == 0 ? EGamepadControl::Touchpad0 : EGamepadControl::Touchpad1;
					const glm::vec3 value = event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP
						? glm::vec3(0.0f)
						: glm::vec3(event.gtouchpad.x, event.gtouchpad.y, std::max(event.gtouchpad.pressure, 0.001f));
					outEvents.push_back(ControlEvent(pad->Info.Id, Gamepad(finger), value, timeUs));
				}
				break;

			case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
				// Hundreds a second: only the latest of the frame is reported, below
				if (Pad* pad = FindPad(event.gsensor.which))
				{
					const glm::vec3 data(event.gsensor.data[0], event.gsensor.data[1], event.gsensor.data[2]);
					if (event.gsensor.sensor == SDL_SENSOR_GYRO)
					{
						pad->Gyro = data;
						pad->bGyroChanged = true;
					}
					else if (event.gsensor.sensor == SDL_SENSOR_ACCEL)
					{
						pad->Accel = data;
						pad->bAccelChanged = true;
					}
				}
				break;

			case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
				if (Pad* pad = FindPad(event.jbattery.which))
				{
					pad->Info.BatteryPercent = BatteryOf(pad->Handle);
					InputEvent changed;
					changed.Kind = EInputEventKind::DeviceChanged;
					changed.TimeUs = timeUs;
					changed.Device = pad->Info;
					outEvents.push_back(std::move(changed));
				}
				break;

			default:
				break;
			}
		}

		const uint64_t now = NowUs();
		for (Pad& pad : Pads)
		{
			if (pad.bGyroChanged)
			{
				outEvents.push_back(ControlEvent(pad.Info.Id, Gamepad(EGamepadControl::Gyro), pad.Gyro, now));
				pad.bGyroChanged = false;
			}
			if (pad.bAccelChanged)
			{
				outEvents.push_back(ControlEvent(pad.Info.Id, Gamepad(EGamepadControl::Accel), pad.Accel, now));
				pad.bAccelChanged = false;
			}
		}
	}

	void SdlGamepadBackend::ApplyOutput(InputDeviceId device, const DeviceOutput& output)
	{
		Pad* pad = FindPad(device);
		if (!pad)
		{
			return;
		}

		const auto check = [pad](bool bDone, const char* what)
		{
			if (!bDone)
			{
				LOG_WARNING("Input: {0} on {1} failed: {2}", what, pad->Info.Name, SDL_GetError());
			}
		};

		if (output.Rumble)
		{
			check(SDL_RumbleGamepad(pad->Handle, Motor(output.Rumble->Low), Motor(output.Rumble->High), output.Rumble->DurationMs), "Rumble");
		}
		if (output.TriggerRumble)
		{
			check(SDL_RumbleGamepadTriggers(pad->Handle, Motor(output.TriggerRumble->Left), Motor(output.TriggerRumble->Right),
					  output.TriggerRumble->DurationMs),
				"Trigger rumble");
		}
		if (output.LeftTriggerEffect || output.RightTriggerEffect)
		{
			const std::array<uint8_t, 47> report = MakeDualSenseTriggerReport(output.LeftTriggerEffect, output.RightTriggerEffect);
			check(SDL_SendGamepadEffect(pad->Handle, report.data(), static_cast<int>(report.size())), "Trigger effect");
		}
		if (output.LightColor)
		{
			const glm::vec3& color = *output.LightColor;
			check(SDL_SetGamepadLED(pad->Handle, Channel(color.r), Channel(color.g), Channel(color.b)), "Light");
		}
		if (output.PlayerIndex)
		{
			check(SDL_SetGamepadPlayerIndex(pad->Handle, *output.PlayerIndex), "Player index");
		}
	}
}

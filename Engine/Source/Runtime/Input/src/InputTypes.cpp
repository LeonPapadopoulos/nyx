#include "InputTypes.h"

#include "StringUtil.h"

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>

namespace Nyx::Input
{
	namespace
	{
		constexpr std::array<const char*, static_cast<size_t>(EKey::Count)> KeyNames = {
#define NYX_INPUT_KEY_NAME(name) #name,
			NYX_INPUT_KEYS(NYX_INPUT_KEY_NAME)
#undef NYX_INPUT_KEY_NAME
		};

		constexpr std::array<const char*, static_cast<size_t>(EMouseControl::Count)> MouseControlNames = {
			"Left",
			"Right",
			"Middle",
			"Button4",
			"Button5",
			"Delta",
			"Wheel",
		};

		constexpr std::array<const char*, static_cast<size_t>(EGamepadControl::Count)> GamepadControlNames = {
			"FaceSouth",
			"FaceEast",
			"FaceWest",
			"FaceNorth",
			"Back",
			"Guide",
			"Start",
			"LeftStickButton",
			"RightStickButton",
			"LeftShoulder",
			"RightShoulder",
			"DpadUp",
			"DpadDown",
			"DpadLeft",
			"DpadRight",
			"Misc",
			"RightPaddle1",
			"LeftPaddle1",
			"RightPaddle2",
			"LeftPaddle2",
			"TouchpadButton",
			"LeftStick",
			"RightStick",
			"LeftTrigger",
			"RightTrigger",
			"Gyro",
			"Accel",
			"Touchpad0",
			"Touchpad1",
		};

		constexpr std::array<EDeviceKind, 3> DeviceKinds = { EDeviceKind::Keyboard, EDeviceKind::Mouse, EDeviceKind::Gamepad };

		const char* GetControlName(ControlId control)
		{
			if (control.Index >= GetControlCount(control.Device))
			{
				return nullptr;
			}
			switch (control.Device)
			{
			case EDeviceKind::Keyboard: return KeyNames[control.Index];
			case EDeviceKind::Mouse:    return MouseControlNames[control.Index];
			case EDeviceKind::Gamepad:  return GamepadControlNames[control.Index];
			case EDeviceKind::Count:    break;
			}
			return nullptr;
		}
	}

	uint16_t GetControlCount(EDeviceKind device)
	{
		switch (device)
		{
		case EDeviceKind::Keyboard: return static_cast<uint16_t>(EKey::Count);
		case EDeviceKind::Mouse:    return static_cast<uint16_t>(EMouseControl::Count);
		case EDeviceKind::Gamepad:  return static_cast<uint16_t>(EGamepadControl::Count);
		case EDeviceKind::Count:    break;
		}
		return 0;
	}

	bool IsButton(ControlId control)
	{
		switch (control.Device)
		{
		case EDeviceKind::Keyboard: return control.Index < static_cast<uint16_t>(EKey::Count);
		case EDeviceKind::Mouse:    return control.Index < static_cast<uint16_t>(EMouseControl::Delta);
		case EDeviceKind::Gamepad:  return control.Index < static_cast<uint16_t>(EGamepadControl::LeftStick);
		case EDeviceKind::Count:    break;
		}
		return false;
	}

	bool IsRelative(ControlId control)
	{
		return control == Mouse(EMouseControl::Delta) || control == Mouse(EMouseControl::Wheel);
	}

	const char* GetDeviceKindName(EDeviceKind device)
	{
		switch (device)
		{
		case EDeviceKind::Keyboard: return "Keyboard";
		case EDeviceKind::Mouse:    return "Mouse";
		case EDeviceKind::Gamepad:  return "Gamepad";
		case EDeviceKind::Count:    break;
		}
		return "Unknown";
	}

	const char* GetGamepadModelName(EGamepadModel model)
	{
		switch (model)
		{
		case EGamepadModel::Unknown:       return "Unknown";
		case EGamepadModel::Xbox360:       return "Xbox 360";
		case EGamepadModel::XboxOne:       return "Xbox One";
		case EGamepadModel::DualShock4:    return "DualShock 4";
		case EGamepadModel::DualSense:     return "DualSense";
		case EGamepadModel::DualSenseEdge: return "DualSense Edge";
		case EGamepadModel::Generic:       return "Generic";
		}
		return "Unknown";
	}

	const char* GetConnectionName(EConnection connection)
	{
		switch (connection)
		{
		case EConnection::Unknown:  return "Unknown";
		case EConnection::Wired:    return "Wired";
		case EConnection::Wireless: return "Wireless";
		}
		return "Unknown";
	}

	std::string GetControlPath(ControlId control)
	{
		const char* name = GetControlName(control);
		std::string path = GetDeviceKindName(control.Device);
		path += '/';
		path += name ? name : std::to_string(control.Index);
		return path;
	}

	std::optional<ControlId> ParseControlPath(std::string_view path)
	{
		const size_t slash = path.find('/');
		if (slash == std::string_view::npos)
		{
			return std::nullopt;
		}

		const std::string_view deviceName = path.substr(0, slash);
		const std::string_view controlName = path.substr(slash + 1);

		for (EDeviceKind device : DeviceKinds)
		{
			if (!EqualsIgnoringCase(deviceName, GetDeviceKindName(device)))
			{
				continue;
			}

			for (uint16_t index = 0; index < GetControlCount(device); ++index)
			{
				const ControlId control{ device, index };
				if (EqualsIgnoringCase(controlName, GetControlName(control)))
				{
					return control;
				}
			}
		}

		return std::nullopt;
	}

	InputDeviceId MakeDeviceId()
	{
		static std::atomic<uint32_t> lastId{ 0 };
		return InputDeviceId{ ++lastId };
	}

	uint64_t NowUs()
	{
		const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count());
	}

	const char* GetCursorModeName(ECursorMode mode)
	{
		switch (mode)
		{
		case ECursorMode::Free:     return "Free";
		case ECursorMode::Hidden:   return "Hidden";
		case ECursorMode::Captured: return "Captured";
		}
		return "Unknown";
	}
}

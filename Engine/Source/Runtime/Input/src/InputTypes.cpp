#include "InputTypes.h"

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

		constexpr std::array<EDeviceKind, 2> DeviceKinds = { EDeviceKind::Keyboard, EDeviceKind::Mouse };

		const char* GetControlName(ControlId control)
		{
			if (control.Index >= GetControlCount(control.Device))
			{
				return nullptr;
			}
			return control.Device == EDeviceKind::Keyboard ? KeyNames[control.Index] : MouseControlNames[control.Index];
		}

		bool EqualsIgnoringCase(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
			{
				return false;
			}
			for (size_t i = 0; i < a.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				{
					return false;
				}
			}
			return true;
		}
	}

	uint16_t GetControlCount(EDeviceKind device)
	{
		switch (device)
		{
		case EDeviceKind::Keyboard: return static_cast<uint16_t>(EKey::Count);
		case EDeviceKind::Mouse:    return static_cast<uint16_t>(EMouseControl::Count);
		}
		return 0;
	}

	bool IsButton(ControlId control)
	{
		return !IsRelative(control) && control.Index < GetControlCount(control.Device);
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

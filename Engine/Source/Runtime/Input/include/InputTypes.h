#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The vocabulary of the input system: devices, the controls they have and the names of those
// controls, and the events backends report.
namespace Nyx::Input
{
	enum class EDeviceKind : uint8_t
	{
		Keyboard,
		Mouse,
		Gamepad,
		Count
	};

	// Keys by their position on the keyboard, named after the US layout. Keyboard/Z is the key
	// right of the left Shift on any layout, also where a German keyboard says Y; WASD sits under
	// the left hand on QWERTZ and AZERTY too. World1 and World2 are keys a US keyboard doesn't
	// have, e.g. the one left of Z on a German keyboard (<>).
	// clang-format off
#define NYX_INPUT_KEYS(KEY)                                                                            \
	KEY(Space) KEY(Apostrophe) KEY(Comma) KEY(Minus) KEY(Period) KEY(Slash)                            \
	KEY(Digit0) KEY(Digit1) KEY(Digit2) KEY(Digit3) KEY(Digit4)                                        \
	KEY(Digit5) KEY(Digit6) KEY(Digit7) KEY(Digit8) KEY(Digit9)                                        \
	KEY(Semicolon) KEY(Equal)                                                                          \
	KEY(A) KEY(B) KEY(C) KEY(D) KEY(E) KEY(F) KEY(G) KEY(H) KEY(I) KEY(J) KEY(K) KEY(L) KEY(M)         \
	KEY(N) KEY(O) KEY(P) KEY(Q) KEY(R) KEY(S) KEY(T) KEY(U) KEY(V) KEY(W) KEY(X) KEY(Y) KEY(Z)         \
	KEY(LeftBracket) KEY(Backslash) KEY(RightBracket) KEY(GraveAccent) KEY(World1) KEY(World2)         \
	KEY(Escape) KEY(Enter) KEY(Tab) KEY(Backspace) KEY(Insert) KEY(Delete)                             \
	KEY(Right) KEY(Left) KEY(Down) KEY(Up) KEY(PageUp) KEY(PageDown) KEY(Home) KEY(End)                \
	KEY(CapsLock) KEY(ScrollLock) KEY(NumLock) KEY(PrintScreen) KEY(Pause)                             \
	KEY(F1) KEY(F2) KEY(F3) KEY(F4) KEY(F5) KEY(F6) KEY(F7) KEY(F8) KEY(F9) KEY(F10) KEY(F11) KEY(F12) \
	KEY(Numpad0) KEY(Numpad1) KEY(Numpad2) KEY(Numpad3) KEY(Numpad4)                                   \
	KEY(Numpad5) KEY(Numpad6) KEY(Numpad7) KEY(Numpad8) KEY(Numpad9)                                   \
	KEY(NumpadDecimal) KEY(NumpadDivide) KEY(NumpadMultiply) KEY(NumpadSubtract)                       \
	KEY(NumpadAdd) KEY(NumpadEnter) KEY(NumpadEqual)                                                   \
	KEY(LeftShift) KEY(LeftControl) KEY(LeftAlt) KEY(LeftSuper)                                        \
	KEY(RightShift) KEY(RightControl) KEY(RightAlt) KEY(RightSuper)                                    \
	KEY(Menu)
	// clang-format on

	enum class EKey : uint16_t
	{
#define NYX_INPUT_KEY_ENUM(name) name,
		NYX_INPUT_KEYS(NYX_INPUT_KEY_ENUM)
#undef NYX_INPUT_KEY_ENUM
		Count
	};

	enum class EMouseControl : uint16_t
	{
		Left,
		Right,
		Middle,
		Button4,
		Button5,
		// How far the mouse moved, in pixels (x right, y down); the sum of the frame
		Delta,
		// How far the wheel turned, in notches (y: away from the user; x: sideways, where the wheel
		// tilts); the sum of the frame
		Wheel,
		Count
	};

	// Face buttons are named by where they are: South is A on an Xbox pad and Cross on a
	// DualShock or DualSense. What is printed on them only matters for prompts.
	enum class EGamepadControl : uint16_t
	{
		FaceSouth,
		FaceEast,
		FaceWest,
		FaceNorth,
		Back,  // View (Xbox), Share (DualShock 4), Create (DualSense)
		Guide, // Xbox button, PS button
		Start, // Menu (Xbox), Options
		LeftStickButton,
		RightStickButton,
		LeftShoulder,
		RightShoulder,
		DpadUp,
		DpadDown,
		DpadLeft,
		DpadRight,
		Misc, // Share (Xbox Series), Mute (DualSense)
		RightPaddle1,
		LeftPaddle1,
		RightPaddle2,
		LeftPaddle2,
		TouchpadButton,
		// Sticks: x right, y up, -1 to 1, without dead zone
		LeftStick,
		RightStick,
		// Triggers: 0 to 1
		LeftTrigger,
		RightTrigger,
		// Radians per second around x (pitch), y (yaw), z (roll), where the pad has a gyro
		Gyro,
		// Metres per second squared, gravity included, where the pad has an accelerometer
		Accel,
		// A finger on the touchpad: x right and y down, 0 to 1, and z its pressure; all 0 without a finger
		Touchpad0,
		Touchpad1,
		Count
	};

	// One control of one kind of device, e.g. Keyboard/W or Mouse/Right
	struct ControlId
	{
		EDeviceKind Device = EDeviceKind::Keyboard;
		uint16_t Index = 0;

		bool operator==(const ControlId&) const = default;
	};

	constexpr ControlId Key(EKey key)
	{
		return ControlId{ EDeviceKind::Keyboard, static_cast<uint16_t>(key) };
	}

	constexpr ControlId Mouse(EMouseControl control)
	{
		return ControlId{ EDeviceKind::Mouse, static_cast<uint16_t>(control) };
	}

	constexpr ControlId Gamepad(EGamepadControl control)
	{
		return ControlId{ EDeviceKind::Gamepad, static_cast<uint16_t>(control) };
	}

	// How many controls a kind of device has; ControlId::Index is below it
	uint16_t GetControlCount(EDeviceKind device);

	// Buttons are up (0) or down (1). The rest are analog: sticks, triggers, sensors, the mouse's
	// movement.
	bool IsButton(ControlId control);

	// Relative controls report how much they changed, e.g. Mouse/Delta. A frame sums them up and
	// starts again at zero.
	bool IsRelative(ControlId control);

	// "Keyboard", "Mouse", "Gamepad"
	const char* GetDeviceKindName(EDeviceKind device);

	// The control's name in data, logs and the debug window: "Keyboard/W", "Mouse/Delta"
	std::string GetControlPath(ControlId control);

	// Reads a path as GetControlPath() writes it, ignoring case. Nothing for an unknown path.
	std::optional<ControlId> ParseControlPath(std::string_view path);

	// A device for as long as it is connected: one that comes back gets a new id. 0 is no device.
	struct InputDeviceId
	{
		uint32_t Value = 0;

		bool IsValid() const
		{
			return Value != 0;
		}

		bool operator==(const InputDeviceId&) const = default;
	};

	// A new id, unique in the process; backends take their devices' ids from here
	InputDeviceId MakeDeviceId();

	enum class EGamepadModel : uint8_t
	{
		Unknown,
		Xbox360,
		XboxOne, // Xbox One and Series pads
		DualShock4,
		DualSense,
		DualSenseEdge,
		Generic, // Another pad SDL knows how to read
	};

	const char* GetGamepadModelName(EGamepadModel model);

	enum class EConnection : uint8_t
	{
		Unknown,
		Wired,
		Wireless, // Bluetooth or a wireless adapter
	};

	const char* GetConnectionName(EConnection connection);

	// What a device can do besides reporting its controls; a bit each
	enum class EDeviceCapability : uint32_t
	{
		None = 0,
		Rumble = 1 << 0,
		TriggerRumble = 1 << 1,    // Xbox impulse triggers, DualSense
		AdaptiveTriggers = 1 << 2, // DualSense trigger effects
		Gyro = 1 << 3,
		Accelerometer = 1 << 4,
		Touchpad = 1 << 5,
		Lightbar = 1 << 6,   // An LED of any colour
		PlayerLeds = 1 << 7, // LEDs that show the player's number
	};

	constexpr EDeviceCapability operator|(EDeviceCapability a, EDeviceCapability b)
	{
		return static_cast<EDeviceCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr bool HasCapability(EDeviceCapability capabilities, EDeviceCapability capability)
	{
		return (static_cast<uint32_t>(capabilities) & static_cast<uint32_t>(capability)) != 0;
	}

	struct InputDeviceInfo
	{
		InputDeviceId Id;
		EDeviceKind Kind = EDeviceKind::Keyboard;
		std::string Name;    // "Keyboard", "DualSense Wireless Controller"
		std::string Backend; // "GLFW", "SDL", "Virtual"

		// Gamepads
		EGamepadModel Model = EGamepadModel::Unknown;
		EConnection Connection = EConnection::Unknown;
		EDeviceCapability Capabilities = EDeviceCapability::None;
		// 0 to 100; unknown, e.g. on a cable
		std::optional<int> BatteryPercent;
		// The same for the same pad when it comes back (serial number or system path), for giving
		// a player their pad back
		std::string StableKey;
	};

	enum class EInputEventKind : uint8_t
	{
		DeviceAdded,   // Device: the new device
		DeviceRemoved, // Device.Id: the device that is gone
		DeviceChanged, // Device: what is known about it now, e.g. its battery
		Control,       // Device.Id, Control, Value
		FocusChanged,  // bFocused: whether one of the program's windows has the focus now
	};

	// What a backend reports, in the order it happened
	struct InputEvent
	{
		EInputEventKind Kind = EInputEventKind::Control;

		// When it happened, in microseconds of NowUs()
		uint64_t TimeUs = 0;

		// The whole device for DeviceAdded and DeviceChanged, only its id otherwise
		InputDeviceInfo Device;

		ControlId Control;

		// Buttons: 1 down, 0 up. Relative controls: how much they changed. Analog: the value now.
		glm::vec3 Value{ 0.0f };

		bool bFocused = false;
	};

	// The input system's clock, in microseconds, for event times and frames
	uint64_t NowUs();

	enum class ECursorMode : uint8_t
	{
		Free,     // The normal cursor
		Hidden,   // Over the window it's invisible, but it still moves and leaves the window
		Captured, // Invisible and held in place; only Mouse/Delta tells how the mouse moves (raw motion where there is)
	};

	const char* GetCursorModeName(ECursorMode mode);

	// What the cursor should be like, and who wants it so, for the debug window and the log
	struct CursorRequest
	{
		ECursorMode Mode = ECursorMode::Free;

		// The backend's window it is about, for GLFW a GLFWwindow* (a scene view dragged out of the
		// editor has its own). Null: the main window.
		void* Window = nullptr;

		std::string Owner;
	};
}

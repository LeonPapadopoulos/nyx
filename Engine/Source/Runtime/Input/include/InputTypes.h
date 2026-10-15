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

	// How many controls a kind of device has; ControlId::Index is below it
	uint16_t GetControlCount(EDeviceKind device);

	// Buttons are up (0) or down (1)
	bool IsButton(ControlId control);

	// Relative controls report how much they changed, e.g. Mouse/Delta. A frame sums them up and
	// starts again at zero.
	bool IsRelative(ControlId control);

	// "Keyboard", "Mouse"
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

	struct InputDeviceInfo
	{
		InputDeviceId Id;
		EDeviceKind Kind = EDeviceKind::Keyboard;
		std::string Name;    // "Keyboard", later e.g. "DualSense Wireless Controller"
		std::string Backend; // "GLFW", "Virtual"
	};

	enum class EInputEventKind : uint8_t
	{
		DeviceAdded,   // Device: the new device
		DeviceRemoved, // Device.Id: the device that is gone
		Control,       // Device.Id, Control, Value
		FocusChanged,  // bFocused: whether one of the program's windows has the focus now
	};

	// What a backend reports, in the order it happened
	struct InputEvent
	{
		EInputEventKind Kind = EInputEventKind::Control;

		// When it happened, in microseconds of NowUs()
		uint64_t TimeUs = 0;

		// The whole device for DeviceAdded, only its id otherwise
		InputDeviceInfo Device;

		ControlId Control;

		// Buttons: 1 down, 0 up. Relative controls: how much they changed.
		glm::vec2 Value{ 0.0f };

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

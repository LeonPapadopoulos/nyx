#pragma once

#include "InputTypes.h"

#include <cstdint>
#include <vector>

namespace Nyx::Input
{
	// One control in one frame
	struct ControlState
	{
		// Buttons: 1 down, 0 up. Relative controls: the sum of the frame. Analog: the last value.
		glm::vec3 Value{ 0.0f };

		// Value at the end of the previous frame
		glm::vec3 PreviousValue{ 0.0f };

		// How often the button went down and up in this frame. A tap shorter than a frame is one
		// of each, while the button is up at the end of the frame.
		uint16_t Presses = 0;
		uint16_t Releases = 0;

		// When the button last went down and up, from the events, not the frame: holds and taps
		// are timed in real time at any frame rate. 0 if it never did.
		uint64_t LastPressUs = 0;
		uint64_t LastReleaseUs = 0;
	};

	// The state of every device's controls in one frame, made by InputSystem::BeginFrame() from the
	// events since the frame before.
	class InputFrame
	{
	public:
		struct DeviceState
		{
			InputDeviceInfo Info;
			std::vector<ControlState> Controls;

			// Removed in this frame: its held buttons count as released here, and it is gone from
			// the next frame on.
			bool bRemoved = false;
		};

		// Of all devices of the control's kind together: a button is down while any of them holds
		// it, presses and relative values add up, and of analog values (sticks, triggers) the
		// longest counts. A control no device has is up and zero.
		ControlState GetState(ControlId control) const;

		// Of one device; up and zero for a device that isn't there
		ControlState GetState(InputDeviceId device, ControlId control) const;

		bool IsDown(ControlId control) const;

		// Went down / up at least once in this frame
		bool WasPressed(ControlId control) const;
		bool WasReleased(ControlId control) const;

		glm::vec3 GetValue(ControlId control) const;

		const std::vector<DeviceState>& GetDevices() const
		{
			return Devices;
		}

		const DeviceState* FindDevice(InputDeviceId device) const;

		// Whether one of the program's windows has the focus. Without it, no key or mouse button
		// is down; gamepads keep reporting (the Unfocused context takes them).
		bool HasFocus() const
		{
			return bFocused;
		}

		uint64_t GetFrameIndex() const
		{
			return FrameIndex;
		}

		// When the frame began, in microseconds of NowUs()
		uint64_t GetTimeUs() const
		{
			return TimeUs;
		}

		// The events this frame was made from, in order
		const std::vector<InputEvent>& GetEvents() const
		{
			return Events;
		}

	private:
		friend class InputSystem;

		// Starts the next frame: forgets removed devices, keeps values as the previous ones,
		// clears the counts and the relative values
		void Begin(uint64_t timeUs);

		void Apply(const InputEvent& event);

		DeviceState* FindDevice(InputDeviceId device);

		// Releases every held button of the device, at the given time, and zeroes its sticks and triggers
		static void ReleaseAll(DeviceState& device, uint64_t timeUs);

		std::vector<DeviceState> Devices;
		std::vector<InputEvent> Events;
		bool bFocused = true;
		uint64_t FrameIndex = 0;
		uint64_t TimeUs = 0;
	};
}

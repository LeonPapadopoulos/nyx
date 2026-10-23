#pragma once

#include "InputBackend.h"

#include <string>
#include <vector>

namespace Nyx::Input
{
	// Devices made by code, e.g. for tests and later for replaying recordings. Nothing after the
	// backend can tell them from real ones. Every call takes the time it happened at, in
	// microseconds of NowUs() or made up, as long as it doesn't go back.
	class VirtualInputBackend final : public IInputBackend
	{
	public:
		InputDeviceId AddDevice(EDeviceKind kind, std::string name, uint64_t timeUs = 0);
		void RemoveDevice(InputDeviceId device, uint64_t timeUs = 0);

		void Press(InputDeviceId device, ControlId control, uint64_t timeUs = 0);
		void Release(InputDeviceId device, ControlId control, uint64_t timeUs = 0);

		// Buttons: 1 down, 0 up. Relative controls: how much they changed. Analog: the value now.
		void SetValue(InputDeviceId device, ControlId control, glm::vec3 value, uint64_t timeUs = 0);
		void SetValue(InputDeviceId device, ControlId control, glm::vec2 value, uint64_t timeUs = 0)
		{
			SetValue(device, control, glm::vec3(value, 0.0f), timeUs);
		}

		// What the input system asked the device to do, e.g. rumble, in the order it asked
		const std::vector<std::pair<InputDeviceId, DeviceOutput>>& GetOutputs() const
		{
			return Outputs;
		}

		// Changes what is known about a device, e.g. its model, capabilities or battery; its id and
		// kind stay
		void SetDeviceInfo(InputDeviceId device, InputDeviceInfo info, uint64_t timeUs = 0);

		void SetFocus(bool bFocused, uint64_t timeUs = 0);

		// An event as another backend reported it, e.g. from a recording or another program's
		// input (InputReplica): its device ids are kept
		void Push(const InputEvent& event)
		{
			Pending.push_back(event);
		}

		const char* GetName() const override
		{
			return "Virtual";
		}

		void Poll(std::vector<InputEvent>& outEvents) override;
		void ApplyCursor(const CursorRequest& request) override;
		void ApplyOutput(InputDeviceId device, const DeviceOutput& output) override;

		// What the input system last made the cursor, and how often it applied one
		const CursorRequest& GetAppliedCursor() const
		{
			return AppliedCursor;
		}

		int GetCursorApplications() const
		{
			return CursorApplications;
		}

	private:
		std::vector<InputEvent> Pending;
		std::vector<InputDeviceInfo> Devices;
		std::vector<std::pair<InputDeviceId, DeviceOutput>> Outputs;
		CursorRequest AppliedCursor;
		int CursorApplications = 0;
	};
}

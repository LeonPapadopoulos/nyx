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

		// Buttons: 1 down, 0 up. Relative controls: how much they changed.
		void SetValue(InputDeviceId device, ControlId control, glm::vec2 value, uint64_t timeUs = 0);

		void SetFocus(bool bFocused, uint64_t timeUs = 0);

		const char* GetName() const override
		{
			return "Virtual";
		}

		void Poll(std::vector<InputEvent>& outEvents) override;
		void ApplyCursor(const CursorRequest& request) override;

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
		CursorRequest AppliedCursor;
		int CursorApplications = 0;
	};
}

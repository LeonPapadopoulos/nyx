#include "InputFrame.h"

#include <algorithm>

namespace Nyx::Input
{
	ControlState InputFrame::GetState(ControlId control) const
	{
		ControlState merged;
		const bool bRelative = IsRelative(control);
		const bool bButton = IsButton(control);

		for (const DeviceState& device : Devices)
		{
			if (device.Info.Kind != control.Device || control.Index >= device.Controls.size())
			{
				continue;
			}

			const ControlState& state = device.Controls[control.Index];
			if (bRelative)
			{
				merged.Value += state.Value;
				merged.PreviousValue += state.PreviousValue;
			}
			else if (bButton)
			{
				merged.Value = glm::max(merged.Value, state.Value);
				merged.PreviousValue = glm::max(merged.PreviousValue, state.PreviousValue);
			}
			else
			{
				// Two pads' sticks don't add up: the one pushed further counts
				merged.Value = glm::length(state.Value) > glm::length(merged.Value) ? state.Value : merged.Value;
				merged.PreviousValue =
					glm::length(state.PreviousValue) > glm::length(merged.PreviousValue) ? state.PreviousValue : merged.PreviousValue;
			}
			merged.Presses = static_cast<uint16_t>(merged.Presses + state.Presses);
			merged.Releases = static_cast<uint16_t>(merged.Releases + state.Releases);
			merged.LastPressUs = std::max(merged.LastPressUs, state.LastPressUs);
			merged.LastReleaseUs = std::max(merged.LastReleaseUs, state.LastReleaseUs);
		}

		return merged;
	}

	ControlState InputFrame::GetState(InputDeviceId device, ControlId control) const
	{
		const DeviceState* state = FindDevice(device);
		if (!state || state->Info.Kind != control.Device || control.Index >= state->Controls.size())
		{
			return ControlState{};
		}
		return state->Controls[control.Index];
	}

	bool InputFrame::IsDown(ControlId control) const
	{
		return GetState(control).Value.x != 0.0f;
	}

	bool InputFrame::WasPressed(ControlId control) const
	{
		return GetState(control).Presses > 0;
	}

	bool InputFrame::WasReleased(ControlId control) const
	{
		return GetState(control).Releases > 0;
	}

	glm::vec3 InputFrame::GetValue(ControlId control) const
	{
		return GetState(control).Value;
	}

	const InputFrame::DeviceState* InputFrame::FindDevice(InputDeviceId device) const
	{
		const auto found = std::find_if(Devices.begin(), Devices.end(),
			[device](const DeviceState& state)
			{
				return state.Info.Id == device;
			});
		return found != Devices.end() ? &*found : nullptr;
	}

	InputFrame::DeviceState* InputFrame::FindDevice(InputDeviceId device)
	{
		return const_cast<DeviceState*>(std::as_const(*this).FindDevice(device));
	}

	void InputFrame::Begin(uint64_t timeUs)
	{
		std::erase_if(Devices,
			[](const DeviceState& device)
			{
				return device.bRemoved;
			});

		for (DeviceState& device : Devices)
		{
			for (uint16_t index = 0; index < device.Controls.size(); ++index)
			{
				ControlState& state = device.Controls[index];
				state.PreviousValue = state.Value;
				state.Presses = 0;
				state.Releases = 0;
				if (IsRelative(ControlId{ device.Info.Kind, index }))
				{
					state.Value = glm::vec3(0.0f);
				}
			}
		}

		Events.clear();
		TimeUs = timeUs;
		++FrameIndex;
	}

	void InputFrame::Apply(const InputEvent& event)
	{
		Events.push_back(event);

		switch (event.Kind)
		{
		case EInputEventKind::DeviceAdded:
		{
			if (!event.Device.Id.IsValid() || FindDevice(event.Device.Id))
			{
				return;
			}

			DeviceState device;
			device.Info = event.Device;
			device.Controls.resize(GetControlCount(event.Device.Kind));
			Devices.push_back(std::move(device));
			return;
		}

		case EInputEventKind::DeviceChanged:
		{
			DeviceState* device = FindDevice(event.Device.Id);
			if (device && !device->bRemoved && device->Info.Kind == event.Device.Kind)
			{
				device->Info = event.Device;
			}
			return;
		}

		case EInputEventKind::DeviceRemoved:
		{
			DeviceState* device = FindDevice(event.Device.Id);
			if (device && !device->bRemoved)
			{
				ReleaseAll(*device, event.TimeUs);
				device->bRemoved = true;
			}
			return;
		}

		case EInputEventKind::FocusChanged:
		{
			bFocused = event.bFocused;
			if (!bFocused)
			{
				// Keys let go of while another program has the focus never reach us. Gamepads
				// belong to no window and keep reporting; the Unfocused context decides about them.
				for (DeviceState& device : Devices)
				{
					if (device.Info.Kind != EDeviceKind::Gamepad)
					{
						ReleaseAll(device, event.TimeUs);
					}
				}
			}
			return;
		}

		case EInputEventKind::Control:
		{
			DeviceState* device = FindDevice(event.Device.Id);
			if (!device || device->bRemoved || device->Info.Kind != event.Control.Device ||
				event.Control.Index >= device->Controls.size() || (!bFocused && device->Info.Kind != EDeviceKind::Gamepad))
			{
				return;
			}

			ControlState& state = device->Controls[event.Control.Index];
			if (IsRelative(event.Control))
			{
				state.Value += event.Value;
				return;
			}
			if (!IsButton(event.Control))
			{
				state.Value = event.Value;
				return;
			}

			const bool bDown = event.Value.x != 0.0f;
			const bool bWasDown = state.Value.x != 0.0f;
			if (bDown && !bWasDown)
			{
				state.Value = glm::vec3(1.0f, 0.0f, 0.0f);
				++state.Presses;
				state.LastPressUs = event.TimeUs;
			}
			else if (!bDown && bWasDown)
			{
				state.Value = glm::vec3(0.0f);
				++state.Releases;
				state.LastReleaseUs = event.TimeUs;
			}
			return;
		}
		}
	}

	void InputFrame::ReleaseAll(DeviceState& device, uint64_t timeUs)
	{
		for (uint16_t index = 0; index < device.Controls.size(); ++index)
		{
			ControlState& state = device.Controls[index];
			const ControlId control{ device.Info.Kind, index };
			if (IsButton(control) && state.Value.x != 0.0f)
			{
				state.Value = glm::vec3(0.0f);
				++state.Releases;
				state.LastReleaseUs = timeUs;
			}
			else if (!IsButton(control) && !IsRelative(control))
			{
				// A stick or trigger isn't held by a device that is gone
				state.Value = glm::vec3(0.0f);
			}
		}
	}
}

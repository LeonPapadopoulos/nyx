#include "VirtualInputBackend.h"

namespace Nyx::Input
{
	InputDeviceId VirtualInputBackend::AddDevice(EDeviceKind kind, std::string name, uint64_t timeUs)
	{
		InputEvent event;
		event.Kind = EInputEventKind::DeviceAdded;
		event.TimeUs = timeUs;
		event.Device.Id = MakeDeviceId();
		event.Device.Kind = kind;
		event.Device.Name = std::move(name);
		event.Device.Backend = GetName();
		Pending.push_back(event);
		return event.Device.Id;
	}

	void VirtualInputBackend::RemoveDevice(InputDeviceId device, uint64_t timeUs)
	{
		InputEvent event;
		event.Kind = EInputEventKind::DeviceRemoved;
		event.TimeUs = timeUs;
		event.Device.Id = device;
		Pending.push_back(std::move(event));
	}

	void VirtualInputBackend::Press(InputDeviceId device, ControlId control, uint64_t timeUs)
	{
		SetValue(device, control, glm::vec2(1.0f, 0.0f), timeUs);
	}

	void VirtualInputBackend::Release(InputDeviceId device, ControlId control, uint64_t timeUs)
	{
		SetValue(device, control, glm::vec2(0.0f), timeUs);
	}

	void VirtualInputBackend::SetValue(InputDeviceId device, ControlId control, glm::vec2 value, uint64_t timeUs)
	{
		InputEvent event;
		event.Kind = EInputEventKind::Control;
		event.TimeUs = timeUs;
		event.Device.Id = device;
		event.Control = control;
		event.Value = value;
		Pending.push_back(std::move(event));
	}

	void VirtualInputBackend::SetFocus(bool bFocused, uint64_t timeUs)
	{
		InputEvent event;
		event.Kind = EInputEventKind::FocusChanged;
		event.TimeUs = timeUs;
		event.bFocused = bFocused;
		Pending.push_back(std::move(event));
	}

	void VirtualInputBackend::Poll(std::vector<InputEvent>& outEvents)
	{
		outEvents.insert(outEvents.end(), std::make_move_iterator(Pending.begin()), std::make_move_iterator(Pending.end()));
		Pending.clear();
	}

	void VirtualInputBackend::ApplyCursor(const CursorRequest& request)
	{
		AppliedCursor = request;
		++CursorApplications;
	}
}

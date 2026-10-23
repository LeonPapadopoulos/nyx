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
		Devices.push_back(event.Device);
		return event.Device.Id;
	}

	void VirtualInputBackend::SetDeviceInfo(InputDeviceId device, InputDeviceInfo info, uint64_t timeUs)
	{
		for (InputDeviceInfo& known : Devices)
		{
			if (known.Id == device)
			{
				info.Id = known.Id;
				info.Kind = known.Kind;
				info.Backend = known.Backend;
				known = info;

				InputEvent event;
				event.Kind = EInputEventKind::DeviceChanged;
				event.TimeUs = timeUs;
				event.Device = std::move(info);
				Pending.push_back(std::move(event));
			}
		}
	}

	void VirtualInputBackend::RemoveDevice(InputDeviceId device, uint64_t timeUs)
	{
		std::erase_if(Devices,
			[device](const InputDeviceInfo& info)
			{
				return info.Id == device;
			});

		InputEvent event;
		event.Kind = EInputEventKind::DeviceRemoved;
		event.TimeUs = timeUs;
		event.Device.Id = device;
		Pending.push_back(std::move(event));
	}

	void VirtualInputBackend::Press(InputDeviceId device, ControlId control, uint64_t timeUs)
	{
		SetValue(device, control, glm::vec3(1.0f, 0.0f, 0.0f), timeUs);
	}

	void VirtualInputBackend::Release(InputDeviceId device, ControlId control, uint64_t timeUs)
	{
		SetValue(device, control, glm::vec3(0.0f), timeUs);
	}

	void VirtualInputBackend::SetValue(InputDeviceId device, ControlId control, glm::vec3 value, uint64_t timeUs)
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

	void VirtualInputBackend::ApplyOutput(InputDeviceId device, const DeviceOutput& output)
	{
		Outputs.push_back({ device, output });
	}

	void VirtualInputBackend::ApplyCursor(const CursorRequest& request)
	{
		AppliedCursor = request;
		++CursorApplications;
	}
}

#include "InputSystem.h"

#include "Log.h"

#include <algorithm>

namespace Nyx::Input
{
	void InputSystem::AddBackend(IInputBackend& backend)
	{
		Backends.push_back(&backend);
	}

	void InputSystem::BeginFrame(uint64_t nowUs)
	{
		Frame.Begin(nowUs);

		std::vector<InputEvent> events;
		for (IInputBackend* backend : Backends)
		{
			backend->Poll(events);
		}

		// Each backend's events are in order already; this interleaves the backends'. Stable, so
		// events at the same time keep the order their backend reported them in.
		std::stable_sort(events.begin(), events.end(),
			[](const InputEvent& a, const InputEvent& b)
			{
				return a.TimeUs < b.TimeUs;
			});

		for (const InputEvent& event : events)
		{
			switch (event.Kind)
			{
			case EInputEventKind::DeviceAdded:
				LOG_INFO("Input: {0} connected ({1}, {2})", event.Device.Name, GetDeviceKindName(event.Device.Kind), event.Device.Backend);
				break;
			case EInputEventKind::DeviceRemoved:
				if (const InputFrame::DeviceState* device = Frame.FindDevice(event.Device.Id))
				{
					LOG_INFO("Input: {0} disconnected", device->Info.Name);
				}
				break;
			default:
				break;
			}

			Frame.Apply(event);
		}
	}

	void InputSystem::EndFrame()
	{
		CursorRequest cursor;
		for (CursorRequest& request : CursorRequests)
		{
			if (request.Mode > cursor.Mode)
			{
				cursor = std::move(request);
			}
		}
		CursorRequests.clear();

		if (cursor.Mode != Cursor.Mode || cursor.Window != Cursor.Window)
		{
			LOG_TRACE("Input: cursor {0}{1}{2}", GetCursorModeName(cursor.Mode), cursor.Owner.empty() ? "" : ", for ", cursor.Owner);
		}
		Cursor = std::move(cursor);

		for (IInputBackend* backend : Backends)
		{
			backend->ApplyCursor(Cursor);
		}
	}

	void InputSystem::RequestCursor(CursorRequest request)
	{
		CursorRequests.push_back(std::move(request));
	}
}

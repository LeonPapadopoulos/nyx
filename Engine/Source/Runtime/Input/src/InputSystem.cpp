#include "InputSystem.h"

#include "Log.h"

#include <algorithm>

namespace Nyx::Input
{
	namespace
	{
		// A frame longer than this (a breakpoint, a stall) counts as this long for the value
		// pipeline. Triggers time with the events and aren't affected.
		constexpr float MaxDeltaSeconds = 0.25f;
	}

	InputSystem::InputSystem()
	{
		User.System = this;

		InputContextDef unfocused;
		unfocused.Name = "Unfocused";
		unfocused.Priority = UnfocusedPriority;
		unfocused.TakesDevices = AllDevices;
		unfocused.Cursor = ECursorMode::Free;
		UnfocusedContext = AddContext(std::move(unfocused));

		InputContextDef imgui;
		imgui.Name = "ImGui";
		imgui.Priority = ImGuiPriority;
		ImGuiContext = AddContext(std::move(imgui));
	}

	void InputSystem::AddBackend(IInputBackend& backend)
	{
		Backends.push_back(&backend);
	}

	ActionHandle InputSystem::AddAction(InputActionDef action)
	{
		Actions.push_back(std::move(action));
		return ActionHandle::Make(static_cast<uint32_t>(Actions.size() - 1), 1);
	}

	ContextHandle InputSystem::AddContext(InputContextDef context)
	{
		Contexts.push_back(std::move(context));
		return ContextHandle::Make(static_cast<uint32_t>(Contexts.size() - 1), 1);
	}

	const InputActionDef& InputSystem::GetAction(ActionHandle action) const
	{
		static const InputActionDef none;
		return action.IsValid() && action.Index() < Actions.size() ? Actions[action.Index()] : none;
	}

	const InputContextDef& InputSystem::GetContext(ContextHandle context) const
	{
		static const InputContextDef none;
		return context.IsValid() && context.Index() < Contexts.size() ? Contexts[context.Index()] : none;
	}

	ActionHandle InputSystem::FindAction(std::string_view name) const
	{
		const auto found = std::find_if(Actions.begin(), Actions.end(),
			[name](const InputActionDef& action)
			{
				return action.Name == name;
			});
		return found != Actions.end() ? ActionHandle::Make(static_cast<uint32_t>(found - Actions.begin()), 1) : ActionHandle{};
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

		const float deltaSeconds = PreviousFrameUs != 0 && nowUs > PreviousFrameUs
			? std::min(static_cast<float>(nowUs - PreviousFrameUs) / 1'000'000.0f, MaxDeltaSeconds)
			: 0.0f;
		PreviousFrameUs = nowUs;

		UpdateSystemContexts();
		User.Evaluate(*this, Frame, deltaSeconds);
	}

	void InputSystem::EndFrame()
	{
		const CursorRequest previous = User.GetCursor();
		User.DecideCursor();

		const CursorRequest& cursor = User.GetCursor();
		if (cursor.Mode != previous.Mode || cursor.Window != previous.Window)
		{
			LOG_TRACE("Input: cursor {0}{1}{2}", GetCursorModeName(cursor.Mode), cursor.Owner.empty() ? "" : ", for ", cursor.Owner);
		}

		for (IInputBackend* backend : Backends)
		{
			backend->ApplyCursor(cursor);
		}
	}

	void InputSystem::UpdateSystemContexts()
	{
		User.SetContextActive(UnfocusedContext, !Frame.HasFocus(), { .Reason = "None of the program's windows has the focus" });

		// ImGui gets the mouse only while the cursor is free: a captured cursor wanders over
		// ImGui's windows without being there
		const bool bTakesKeyboard = ImGuiState.bTypingText;
		const bool bTakesMouse = ImGuiState.bMouseOverUI && User.GetCursor().Mode == ECursorMode::Free;
		const uint32_t devices = (bTakesKeyboard ? DeviceBit(EDeviceKind::Keyboard) : 0u) | (bTakesMouse ? DeviceBit(EDeviceKind::Mouse) : 0u);

		std::string reason = bTakesKeyboard ? "A text field is active" : "";
		if (bTakesMouse)
		{
			reason += reason.empty() ? "The mouse is over ImGui UI" : "; the mouse is over ImGui UI";
		}
		User.SetContextActive(ImGuiContext, devices != 0, { .Reason = std::move(reason), .TakesDevices = devices });
	}
}

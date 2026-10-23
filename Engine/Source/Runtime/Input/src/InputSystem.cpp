#include "InputSystem.h"

#include "BinaryArchive.h"
#include "InputRecording.h"

#include "Log.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace Nyx::Input
{
	namespace
	{
		// A frame longer than this (a breakpoint, a stall) counts as this long for the value
		// pipeline. Triggers time with the events and aren't affected.
		constexpr float MaxDeltaSeconds = 0.25f;
	}

	// A replayed device's id: the recorded one with this bit, so it can't be taken for a device
	// that is connected
	constexpr uint32_t ReplayedDeviceBit = 0x40000000u;

	struct InputSystem::ReplayState
	{
		InputRecording Recording;
		size_t NextRecord = 0;
		size_t ReplayedFrames = 0;
		size_t FrameCount = 0;

		// Recorded time plus this is this program's time
		int64_t TimeOffset = 0;
		bool bStarted = false;

		std::vector<InputDeviceId> Devices;
	};

	InputSystem::~InputSystem() = default;

	void InputSystem::StartReplay(InputRecording recording)
	{
		StopReplay();
		Replay = std::make_unique<ReplayState>();
		Replay->FrameCount = recording.GetFrameCount();
		Replay->Recording = std::move(recording);
		LOG_INFO("Input: replaying {0} frames of {1}'s input", Replay->FrameCount, Replay->Recording.ProgramName);
	}

	void InputSystem::StopReplay()
	{
		if (!Replay)
		{
			return;
		}

		// The recorded devices go in the next frame, letting go of what they held
		LOG_INFO("Input: replay ended after {0} of {1} frames", Replay->ReplayedFrames, Replay->FrameCount);
		for (const InputDeviceId device : Replay->Devices)
		{
			InputEvent removed{ .Kind = EInputEventKind::DeviceRemoved, .TimeUs = Frame.GetTimeUs() };
			removed.Device.Id = device;
			ReplayEnded.push_back(removed);
		}
		Replay.reset();
	}

	size_t InputSystem::GetReplayedFrames() const
	{
		return Replay ? Replay->ReplayedFrames : 0;
	}

	size_t InputSystem::GetReplayFrameCount() const
	{
		return Replay ? Replay->FrameCount : 0;
	}

	bool InputSystem::TakeReplayFrame(uint64_t& inOutNowUs, std::vector<InputEvent>& inOutEvents)
	{
		// The devices' controls and the focus don't count while a recording plays
		std::erase_if(inOutEvents,
			[](const InputEvent& event)
			{
				return event.Kind == EInputEventKind::Control || event.Kind == EInputEventKind::FocusChanged;
			});

		// The next frame record; the program's own definitions count, not the recording's
		InputFrameRecord record;
		bool bFound = false;
		std::vector<InputStreamRecord>& records = Replay->Recording.Records;
		while (Replay->NextRecord < records.size() && !bFound)
		{
			const InputStreamRecord& next = records[Replay->NextRecord++];
			if (next.bDefinitions)
			{
				continue;
			}
			Nyx::BinaryReader reader;
			reader.LoadFromMemory(next.Bytes);
			bFound = ReadFrameRecord(reader, record);
		}
		if (!bFound)
		{
			return false;
		}

		// The recorded times, moved to start now. What the devices hold lets go: only the recording counts.
		if (!Replay->bStarted)
		{
			Replay->TimeOffset = static_cast<int64_t>(inOutNowUs) - static_cast<int64_t>(record.TimeUs);
			Replay->bStarted = true;
			for (const InputFrame::DeviceState& device : Frame.GetDevices())
			{
				for (size_t index = 0; index < device.Controls.size() && !device.bRemoved; ++index)
				{
					if (device.Controls[index].Value != glm::vec3(0.0f))
					{
						InputEvent release{ .Kind = EInputEventKind::Control, .TimeUs = inOutNowUs };
						release.Device.Id = device.Info.Id;
						release.Control = ControlId{ device.Info.Kind, static_cast<uint16_t>(index) };
						inOutEvents.push_back(release);
					}
				}
			}
		}
		const auto shift = [this](uint64_t timeUs)
		{
			return timeUs == 0 ? 0 : static_cast<uint64_t>(static_cast<int64_t>(timeUs) + Replay->TimeOffset);
		};
		inOutNowUs = std::max(shift(record.TimeUs), PreviousFrameUs);

		for (InputEvent event : record.Events)
		{
			event.TimeUs = shift(event.TimeUs);
			if (event.Device.Id.IsValid())
			{
				event.Device.Id.Value |= ReplayedDeviceBit;
			}
			if (event.Kind == EInputEventKind::DeviceAdded)
			{
				Replay->Devices.push_back(event.Device.Id);
			}
			inOutEvents.push_back(event);
		}
		ImGuiState = record.ImGui;
		bGamepadsInBackground = record.bGamepadsInBackground;
		++Replay->ReplayedFrames;
		return true;
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

	uint32_t InputSystem::AddFrameObserver(FrameObserver observer)
	{
		Observers.emplace_back(NextObserverId, std::move(observer));
		return NextObserverId++;
	}

	void InputSystem::RemoveFrameObserver(uint32_t id)
	{
		std::erase_if(Observers,
			[id](const auto& observer)
			{
				return observer.first == id;
			});
	}

	ActionHandle InputSystem::AddAction(InputActionDef action)
	{
		++DefinitionsRevision;
		Actions.push_back(std::move(action));
		return ActionHandle::Make(static_cast<uint32_t>(Actions.size() - 1), 1);
	}

	ContextHandle InputSystem::AddContext(InputContextDef context)
	{
		++DefinitionsRevision;
		Contexts.push_back(std::move(context));
		return ContextHandle::Make(static_cast<uint32_t>(Contexts.size() - 1), 1);
	}

	void InputSystem::ReplaceAction(ActionHandle action, InputActionDef def)
	{
		if (action.IsValid() && action.Index() < Actions.size())
		{
			++DefinitionsRevision;
			Actions[action.Index()] = std::move(def);
		}
	}

	void InputSystem::ReplaceContext(ContextHandle context, InputContextDef def)
	{
		if (!context.IsValid() || context.Index() >= Contexts.size())
		{
			return;
		}

		// The same bindings with other tuning: what they remember still fits
		++DefinitionsRevision;
		const std::vector<InputBinding>& old = Contexts[context.Index()].Bindings;
		const bool bSameBindings = std::equal(old.begin(), old.end(), def.Bindings.begin(), def.Bindings.end(),
			[](const InputBinding& a, const InputBinding& b)
			{
				return a.Action == b.Action && a.Control == b.Control && a.Composite == b.Composite && a.Modifiers == b.Modifiers;
			});

		Contexts[context.Index()] = std::move(def);
		if (!bSameBindings)
		{
			User.ForgetBindingStates(context);
		}
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

	ContextHandle InputSystem::FindContext(std::string_view name) const
	{
		const auto found = std::find_if(Contexts.begin(), Contexts.end(),
			[name](const InputContextDef& context)
			{
				return context.Name == name;
			});
		return found != Contexts.end() ? ContextHandle::Make(static_cast<uint32_t>(found - Contexts.begin()), 1) : ContextHandle{};
	}

	void InputSystem::BeginFrame(uint64_t nowUs)
	{
		std::vector<InputEvent> events = std::exchange(ReplayEnded, {});
		for (IInputBackend* backend : Backends)
		{
			const size_t first = events.size();
			backend->Poll(events);
			for (size_t index = first; index < events.size(); ++index)
			{
				if (events[index].Kind == EInputEventKind::DeviceAdded)
				{
					DeviceBackends[events[index].Device.Id.Value] = backend;
				}
			}
		}

		// A recording replaces the devices' input while it plays
		if (Replay && !TakeReplayFrame(nowUs, events))
		{
			StopReplay();
			events.insert(events.end(), ReplayEnded.begin(), ReplayEnded.end());
			ReplayEnded.clear();
		}

		// A replay's clock can run ahead of this program's; time never goes back
		nowUs = std::max(nowUs, PreviousFrameUs);
		Frame.Begin(nowUs);

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
				if (!bLogging)
				{
					break;
				}
				LOG_INFO("Input: {0} connected ({1}, {2})", event.Device.Name, GetDeviceKindName(event.Device.Kind), event.Device.Backend);
				break;
			case EInputEventKind::DeviceRemoved:
				if (const InputFrame::DeviceState* device = bLogging ? Frame.FindDevice(event.Device.Id) : nullptr)
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
		for (const auto& [id, observer] : Observers)
		{
			observer(*this);
		}
		User.Evaluate(*this, Frame, deltaSeconds);
	}

	void InputSystem::EndFrame()
	{
		const CursorRequest previous = User.GetCursor();
		User.DecideCursor();

		const CursorRequest& cursor = User.GetCursor();
		if (bLogging && (cursor.Mode != previous.Mode || cursor.Window != previous.Window))
		{
			LOG_TRACE("Input: cursor {0}{1}{2}", GetCursorModeName(cursor.Mode), cursor.Owner.empty() ? "" : ", for ", cursor.Owner);
		}

		for (IInputBackend* backend : Backends)
		{
			backend->ApplyCursor(cursor);
		}

		SendOutput();
	}

	void InputSystem::SendOutput()
	{
		User.OutputRecords.clear();
		DeviceOutput output = std::exchange(User.PendingOutput, DeviceOutput{});
		if (output.IsEmpty())
		{
			return;
		}

		if (output.Rumble)
		{
			output.Rumble->Low *= User.HapticsStrength;
			output.Rumble->High *= User.HapticsStrength;
		}
		if (output.TriggerRumble)
		{
			output.TriggerRumble->Left *= User.HapticsStrength;
			output.TriggerRumble->Right *= User.HapticsStrength;
		}

		// Each part that was asked for: what a device needs for it, and what it is
		struct Part
		{
			EDeviceCapability Needs;
			const char* Missing;
			std::string What;
		};
		std::array<std::optional<Part>, 6> parts;
		if (output.Rumble)
		{
			parts[0] = Part{ EDeviceCapability::Rumble, "no rumble", DescribeRumble(*output.Rumble) };
		}
		if (output.TriggerRumble)
		{
			parts[1] = Part{ EDeviceCapability::TriggerRumble, "no trigger rumble", DescribeTriggerRumble(*output.TriggerRumble) };
		}
		if (output.LeftTriggerEffect)
		{
			parts[2] = Part{ EDeviceCapability::AdaptiveTriggers, "no adaptive triggers", DescribeTriggerEffect(ETrigger::Left, *output.LeftTriggerEffect) };
		}
		if (output.RightTriggerEffect)
		{
			parts[3] = Part{ EDeviceCapability::AdaptiveTriggers, "no adaptive triggers", DescribeTriggerEffect(ETrigger::Right, *output.RightTriggerEffect) };
		}
		if (output.LightColor)
		{
			parts[4] = Part{ EDeviceCapability::Lightbar, "no light", DescribeLightColor(*output.LightColor) };
		}
		if (output.PlayerIndex)
		{
			parts[5] = Part{ EDeviceCapability::PlayerLeds, "no player LEDs", "Player " + std::to_string(*output.PlayerIndex) };
		}

		bool bAnyGamepad = false;
		for (const InputFrame::DeviceState& device : Frame.GetDevices())
		{
			if (device.Info.Kind != EDeviceKind::Gamepad || device.bRemoved)
			{
				continue;
			}
			bAnyGamepad = true;

			// Whether the pad can do the part; recorded either way
			const std::string name = device.Info.Model != EGamepadModel::Unknown ? GetGamepadModelName(device.Info.Model) : device.Info.Name;
			const auto offer = [&](size_t index)
			{
				if (!parts[index])
				{
					return false;
				}
				const bool bCan = HasCapability(device.Info.Capabilities, parts[index]->Needs);
				User.OutputRecords.push_back({ device.Info.Id, parts[index]->What, bCan, bCan ? std::string() : name + " has " + parts[index]->Missing });
				return bCan;
			};

			DeviceOutput sent;
			sent.Rumble = offer(0) ? output.Rumble : std::nullopt;
			sent.TriggerRumble = offer(1) ? output.TriggerRumble : std::nullopt;
			sent.LeftTriggerEffect = offer(2) ? output.LeftTriggerEffect : std::nullopt;
			sent.RightTriggerEffect = offer(3) ? output.RightTriggerEffect : std::nullopt;
			sent.LightColor = offer(4) ? output.LightColor : std::nullopt;
			sent.PlayerIndex = offer(5) ? output.PlayerIndex : std::nullopt;

			const auto backend = DeviceBackends.find(device.Info.Id.Value);
			if (!sent.IsEmpty() && backend != DeviceBackends.end())
			{
				backend->second->ApplyOutput(device.Info.Id, sent);
			}
		}

		if (!bAnyGamepad)
		{
			for (const std::optional<Part>& asked : parts)
			{
				if (asked)
				{
					User.OutputRecords.push_back({ InputDeviceId{}, asked->What, false, "No gamepad connected" });
				}
			}
		}
	}

	void InputSystem::UpdateSystemContexts()
	{
		if (bGamepadsInBackground)
		{
			User.SetContextActive(UnfocusedContext, !Frame.HasFocus(),
				{ .Reason = "None of the program's windows has the focus; pads drive it anyway",
					.TakesDevices = DeviceBit(EDeviceKind::Keyboard) | DeviceBit(EDeviceKind::Mouse) });
		}
		else
		{
			User.SetContextActive(UnfocusedContext, !Frame.HasFocus(), { .Reason = "None of the program's windows has the focus" });
		}

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

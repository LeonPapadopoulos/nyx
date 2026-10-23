#include "InputHistory.h"

namespace Nyx::Input
{
	InputHistory::InputHistory(size_t frames, size_t entries)
		: MaxFrames(frames)
		, MaxEntries(entries)
	{
	}

	void InputHistory::Record(const InputSystem& input)
	{
		if (bIsPaused)
		{
			return;
		}

		const InputFrame& frame = input.GetFrame();
		const InputUser& user = input.GetUser();

		Frame recorded;
		recorded.FrameIndex = frame.GetFrameIndex();
		recorded.TimeUs = frame.GetTimeUs();
		recorded.Actions.reserve(input.GetActionCount());
		for (uint32_t index = 0; index < input.GetActionCount(); ++index)
		{
			recorded.Actions.push_back(user.Get(ActionHandle::Make(index, 1)));
		}
		recorded.Traces = user.GetTraces();
		recorded.Stack = user.GetStack();
		Frames.push_back(std::move(recorded));
		while (Frames.size() > MaxFrames)
		{
			Frames.pop_front();
		}

		for (const InputEvent& event : frame.GetEvents())
		{
			Entries.push_back({ frame.GetFrameIndex(), event.TimeUs, event, std::nullopt });
		}
		// What the end of the last frame sent
		for (const InputUser::OutputRecord& output : user.GetOutputRecords())
		{
			Entries.push_back({ frame.GetFrameIndex(), frame.GetTimeUs(), std::nullopt, output });
		}
		while (Entries.size() > MaxEntries)
		{
			Entries.pop_front();
		}
	}

	void InputHistory::Clear()
	{
		Frames.clear();
		Entries.clear();
	}

	std::vector<glm::vec3> InputHistory::GetTrail(InputDeviceId device, ControlId control) const
	{
		std::vector<glm::vec3> trail;
		for (const Entry& entry : Entries)
		{
			if (entry.Event && entry.Event->Kind == EInputEventKind::Control && entry.Event->Device.Id == device && entry.Event->Control == control)
			{
				trail.push_back(entry.Event->Value);
			}
		}
		return trail;
	}

	uint64_t InputHistory::GetLastTriggeredUs(ActionHandle action) const
	{
		for (auto frame = Frames.rbegin(); frame != Frames.rend(); ++frame)
		{
			if (action.Index() < frame->Actions.size() && frame->Actions[action.Index()].bTriggered)
			{
				return frame->TimeUs;
			}
		}
		return 0;
	}
}

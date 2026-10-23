#pragma once

#include "InputSystem.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace Nyx::Input
{
	// The last seconds of the user's input, frame by frame, for the debug window: action states,
	// traces and the stack of each frame for the timeline, events and output for the events list,
	// stick positions for the stick plot. Record() once per frame, after the input system's
	// BeginFrame().
	class InputHistory
	{
	public:
		struct Frame
		{
			uint64_t FrameIndex = 0;
			uint64_t TimeUs = 0;
			std::vector<ActionState> Actions;
			std::vector<BindingTrace> Traces;
			std::vector<InputUser::StackEntry> Stack;
		};

		// An event or an output of the user, in one list
		struct Entry
		{
			uint64_t FrameIndex = 0;
			uint64_t TimeUs = 0;
			// Unset for an output
			std::optional<InputEvent> Event;
			std::optional<InputUser::OutputRecord> Output;
		};

		// `frames`: how many frames the timeline keeps; `entries`: how many events and outputs
		explicit InputHistory(size_t frames = 300, size_t entries = 2000);

		void Record(const InputSystem& input);

		// While paused, Record() keeps nothing: the timeline and the events stay as they are
		void SetPaused(bool bPaused)
		{
			bIsPaused = bPaused;
		}

		bool IsPaused() const
		{
			return bIsPaused;
		}

		void Clear();

		const std::deque<Frame>& GetFrames() const
		{
			return Frames;
		}

		const std::deque<Entry>& GetEntries() const
		{
			return Entries;
		}

		// Where the device's stick or other analog control was in the frames kept, oldest first
		std::vector<glm::vec3> GetTrail(InputDeviceId device, ControlId control) const;

		// When the action last fired, in microseconds of NowUs(); 0 if not in the frames kept
		uint64_t GetLastTriggeredUs(ActionHandle action) const;

	private:
		size_t MaxFrames;
		size_t MaxEntries;
		bool bIsPaused = false;
		std::deque<Frame> Frames;
		std::deque<Entry> Entries;
	};
}

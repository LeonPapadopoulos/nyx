#pragma once

#include "ImGuiInput.h"
#include "InputHistory.h"
#include "InputSystem.h"
#include "VirtualInputBackend.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class BinaryReader;
	class BinaryWriter;
}

// An input system's frames as data: what another input system with the same actions and contexts
// needs to decide the same, frame by frame. The router decides from the events, the frame times,
// the stack and ImGui's state alone, so a replica fed with them has the same action phases, traces
// and timeline as the original. The editor's Input window shows a running game that way
// (InputReplica), and recordings will be made of the same records.
//
// Two kinds of records:
// - definitions: every action and context, whenever they changed (an input asset applied, a
//   context added by code);
// - a frame: its time and events, ImGui's state, whether pads drive the program in the
//   background, the stack if it changed (without the system contexts, which follow from the rest),
//   the actions the code consumed for the first time, and what the last frame sent to the pads.
namespace Nyx::Input
{
	struct InputStackRecord
	{
		std::string Context;
		std::string Reason;
		std::optional<uint32_t> TakesDevices;
	};

	struct InputFrameRecord
	{
		uint64_t TimeUs = 0;
		std::vector<InputEvent> Events;
		ImGuiInputState ImGui;
		bool bGamepadsInBackground = false;

		// The stack the frame was decided with, lowest first; unset if it is as in the frame before
		std::optional<std::vector<InputStackRecord>> Stack;

		std::vector<std::string> NewlyConsumed;
		std::vector<InputUser::OutputRecord> Outputs;
	};

	struct InputDefinitionsRecord
	{
		std::vector<InputActionDef> Actions;
		std::vector<InputContextDef> Contexts;
	};

	void WriteFrameRecord(Nyx::BinaryWriter& writer, const InputFrameRecord& record);
	bool ReadFrameRecord(Nyx::BinaryReader& reader, InputFrameRecord& outRecord);
	void WriteDefinitionsRecord(Nyx::BinaryWriter& writer, const InputDefinitionsRecord& record);
	bool ReadDefinitionsRecord(Nyx::BinaryReader& reader, InputDefinitionsRecord& outRecord);

	// One record as written: its kind and its bytes, e.g. one editor link message's payload, or one
	// record of a recording (InputRecording.h)
	struct InputStreamRecord
	{
		bool bDefinitions = false;
		std::vector<std::byte> Bytes;
	};

	// Takes an input system's records, frame by frame, from Start() to Stop(). The input system
	// calls it at each BeginFrame(), with the stack it decides the frame with. Several can watch one
	// input system, e.g. the editor link's and a recording's.
	class InputStreamWriter
	{
	public:
		// The input system must outlive this, or the stream be stopped first
		explicit InputStreamWriter(InputSystem& input);
		~InputStreamWriter();

		InputStreamWriter(const InputStreamWriter&) = delete;
		InputStreamWriter& operator=(const InputStreamWriter&) = delete;

		// From the next frame on. The first frame is a snapshot: the devices with the controls
		// they hold, the focus, the definitions and the whole stack. Buttons held already look
		// pressed in that frame to a replica; from the next frame on, it decides as the original.
		void Start();
		void Stop();

		bool IsStreaming() const
		{
			return bStreaming;
		}

		// The records since the last call, in order
		using Record = InputStreamRecord;
		std::vector<Record> TakeRecords();

	private:
		void OnFrame(const InputSystem& input);

		InputSystem& Input;
		uint32_t ObserverId = 0;
		bool bStreaming = false;
		bool bSnapshotNext = false;
		uint64_t DefinitionsRevision = 0;
		std::vector<InputStackRecord> LastStack;
		std::vector<bool> Consumed;
		std::vector<Record> Records;
	};

	// An input system that decides what another one decided, from its records: the editor's view
	// of a running game. Its devices are virtual; what it sends to the pads goes nowhere.
	class InputReplica
	{
	public:
		InputReplica();

		InputReplica(const InputReplica&) = delete;
		InputReplica& operator=(const InputReplica&) = delete;

		// False if the record is damaged; then nothing changed
		bool ApplyDefinitions(const std::vector<std::byte>& bytes);
		bool ApplyFrame(const std::vector<std::byte>& bytes);
		bool Apply(const InputStreamRecord& record)
		{
			return record.bDefinitions ? ApplyDefinitions(record.Bytes) : ApplyFrame(record.Bytes);
		}

		InputSystem& GetInput()
		{
			return Input;
		}

		InputHistory& GetHistory()
		{
			return History;
		}

		// Frames replayed so far: nothing to show before the first
		uint64_t GetFrameCount() const
		{
			return FrameCount;
		}

	private:
		VirtualInputBackend Backend;
		InputSystem Input;
		InputHistory History;
		uint64_t FrameCount = 0;
	};
}

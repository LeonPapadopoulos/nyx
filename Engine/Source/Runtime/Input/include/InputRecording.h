#pragma once

#include "InputStream.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Input recordings (.nyxinputrec): an input system's stream (InputStream.h) in a file, to replay
// a playtester's session in the editor, or in the game (--input-replay), and to keep a bug as a
// test.
//
// File:   magic "NXIR" (u32) | version (u32) | program name (string) | start (u64, ms since 1970 UTC)
// Then per record, as long as the file goes:
//         kind (u8: 0 definitions, 1 frame) | size (u32) | the record
// The first records are a snapshot: the definitions, then a frame with the devices, what they
// hold and the stack. Each frame is flushed, so a recording is whole up to the last frame before a
// crash.
namespace Nyx::Input
{
	inline constexpr uint32_t InputRecordingMagic = 0x5249584E; // 'NXIR'
	inline constexpr uint32_t InputRecordingVersion = 1;

	// Writes records to a file as they come
	class InputRecorder
	{
	public:
		// Starts a new file, replacing an existing one; programName says whose input it is
		bool Open(const std::filesystem::path& path, const std::string& programName);
		void Write(const InputStreamRecord& record);
		void Close();

		bool IsOpen() const
		{
			return File.is_open();
		}

		const std::filesystem::path& GetPath() const
		{
			return Path;
		}

		uint64_t GetFrameCount() const
		{
			return FrameCount;
		}

	private:
		std::ofstream File;
		std::filesystem::path Path;
		uint64_t FrameCount = 0;
	};

	// Records an input system from Start() to Stop(): its stream into a recorder. Update() once
	// per frame, after BeginFrame(). The input system must outlive it.
	class InputRecordingSession
	{
	public:
		bool Start(InputSystem& input, const std::filesystem::path& path, const std::string& programName);
		void Update();
		void Stop();

		bool IsRecording() const
		{
			return Recorder.IsOpen();
		}

		const InputRecorder& GetRecorder() const
		{
			return Recorder;
		}

	private:
		std::unique_ptr<InputStreamWriter> Writer;
		InputRecorder Recorder;
	};

	struct InputRecording
	{
		std::string ProgramName;
		uint64_t StartTimeMs = 0;
		std::vector<InputStreamRecord> Records;

		size_t GetFrameCount() const;
	};

	// The whole records of a recording; one cut off at the end, e.g. by a crash, is left out. Unset,
	// with the reason, if the file isn't a recording.
	std::optional<InputRecording> LoadInputRecording(const std::filesystem::path& path, std::string* outProblem = nullptr);

	// Where the editor puts its recordings: an "InputRecordings" folder next to it
	std::filesystem::path GetInputRecordingFolder();

	// A new file name in that folder for whose input it is, e.g. "Input_2026-10-14_11-05-03_Game 1.nyxinputrec"
	std::filesystem::path MakeInputRecordingPath(const std::string& source);
}

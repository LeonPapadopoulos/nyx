#include "InputRecording.h"

#include "BinaryArchive.h"
#include "ClockTime.h"
#include "Paths.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace Nyx::Input
{
	// ---------------------------------------------------------------------------------------
	// Writing

	bool InputRecorder::Open(const std::filesystem::path& path, const std::string& programName)
	{
		Close();

		std::error_code ignored;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), ignored);
		}

		File.open(path, std::ios::binary | std::ios::trunc);
		if (!File.is_open())
		{
			return false;
		}

		Nyx::BinaryWriter header;
		header.WriteUInt32(InputRecordingMagic);
		header.WriteUInt32(InputRecordingVersion);
		header.WriteString(programName);
		header.WriteUInt64(GetClockTimeMs());
		File.write(reinterpret_cast<const char*>(header.GetBytes().data()), static_cast<std::streamsize>(header.GetBytes().size()));
		File.flush();

		Path = path;
		FrameCount = 0;
		return File.good();
	}

	void InputRecorder::Write(const InputStreamRecord& record)
	{
		if (!File.is_open())
		{
			return;
		}

		Nyx::BinaryWriter writer;
		writer.WriteUInt8(record.bDefinitions ? 0 : 1);
		writer.WriteUInt32(static_cast<uint32_t>(record.Bytes.size()));
		writer.WriteBytes(record.Bytes.data(), record.Bytes.size());
		File.write(reinterpret_cast<const char*>(writer.GetBytes().data()), static_cast<std::streamsize>(writer.GetBytes().size()));

		// Whole up to the last frame, also after a crash
		if (!record.bDefinitions)
		{
			File.flush();
			++FrameCount;
		}
	}

	void InputRecorder::Close()
	{
		if (File.is_open())
		{
			File.close();
		}
	}

	bool InputRecordingSession::Start(InputSystem& input, const std::filesystem::path& path, const std::string& programName)
	{
		Stop();
		if (!Recorder.Open(path, programName))
		{
			return false;
		}
		Writer = std::make_unique<InputStreamWriter>(input);
		Writer->Start();
		return true;
	}

	void InputRecordingSession::Update()
	{
		if (!Writer)
		{
			return;
		}
		for (const InputStreamRecord& record : Writer->TakeRecords())
		{
			Recorder.Write(record);
		}
	}

	void InputRecordingSession::Stop()
	{
		Update();
		Writer.reset();
		Recorder.Close();
	}

	// ---------------------------------------------------------------------------------------
	// Reading

	size_t InputRecording::GetFrameCount() const
	{
		return static_cast<size_t>(std::count_if(Records.begin(), Records.end(),
			[](const InputStreamRecord& record)
			{
				return !record.bDefinitions;
			}));
	}

	std::optional<InputRecording> LoadInputRecording(const std::filesystem::path& path, std::string* outProblem)
	{
		const auto fail = [&](std::string problem) -> std::optional<InputRecording>
		{
			if (outProblem)
			{
				*outProblem = std::move(problem);
			}
			return std::nullopt;
		};

		Nyx::BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			return fail("can't open '" + path.string() + "'");
		}

		uint32_t magic = 0;
		uint32_t version = 0;
		InputRecording recording;
		if (!reader.ReadUInt32(magic) || magic != InputRecordingMagic)
		{
			return fail("'" + path.string() + "' is not an input recording");
		}
		if (!reader.ReadUInt32(version) || version != InputRecordingVersion)
		{
			return fail("'" + path.string() + "' is an input recording of version " + std::to_string(version) + ", but this build reads version " +
				std::to_string(InputRecordingVersion));
		}
		if (!reader.ReadString(recording.ProgramName) || !reader.ReadUInt64(recording.StartTimeMs))
		{
			return fail("'" + path.string() + "' ends early");
		}

		// A record cut off at the end is left out
		while (reader.GetRemainingSize() > 0)
		{
			uint8_t kind = 0;
			uint32_t size = 0;
			if (!reader.ReadUInt8(kind) || !reader.ReadUInt32(size) || size > reader.GetRemainingSize())
			{
				break;
			}
			InputStreamRecord record;
			record.bDefinitions = kind == 0;
			record.Bytes.resize(size);
			if (!reader.ReadBytes(record.Bytes.data(), size))
			{
				break;
			}
			recording.Records.push_back(std::move(record));
		}
		return recording;
	}

	// ---------------------------------------------------------------------------------------
	// Playback

	bool InputPlayback::Load(const std::filesystem::path& path, std::string* outProblem)
	{
		std::optional<InputRecording> recording = LoadInputRecording(path, outProblem);
		if (!recording)
		{
			return false;
		}

		Path = path;
		Recording = std::move(*recording);
		FrameRecords.clear();
		FrameTimes.clear();
		for (size_t index = 0; index < Recording.Records.size(); ++index)
		{
			const InputStreamRecord& record = Recording.Records[index];
			if (record.bDefinitions)
			{
				continue;
			}

			// A frame record starts with its time
			Nyx::BinaryReader reader;
			reader.LoadFromMemory(record.Bytes);
			uint64_t timeUs = 0;
			reader.ReadUInt64(timeUs);
			FrameRecords.push_back(index);
			FrameTimes.push_back(timeUs);
		}
		SeekTo(0);
		return true;
	}

	void InputPlayback::SetTuning(std::optional<InputAssetData> asset)
	{
		Tuning = std::move(asset);
		const size_t frames = ReplayedFrames;
		SeekTo(frames);
	}

	float InputPlayback::GetSeconds(size_t frame) const
	{
		if (FrameTimes.empty())
		{
			return 0.0f;
		}
		frame = std::min(frame, FrameTimes.size() - 1);
		return static_cast<float>(static_cast<double>(FrameTimes[frame] - FrameTimes.front()) / 1'000'000.0);
	}

	void InputPlayback::ReplayNextFrame()
	{
		if (IsAtEnd())
		{
			return;
		}

		// The definitions before the frame, with the tuning over them, then the frame
		const size_t frameRecord = FrameRecords[ReplayedFrames];
		while (NextRecord <= frameRecord)
		{
			const InputStreamRecord& record = Recording.Records[NextRecord++];
			if (Replica->Apply(record) && record.bDefinitions && Tuning)
			{
				ApplyInputAsset(Replica->GetInput(), *Tuning);
			}
		}
		++ReplayedFrames;
	}

	void InputPlayback::Update(float deltaSeconds)
	{
		if (!bPlaying)
		{
			return;
		}

		PlayedSeconds += deltaSeconds;
		while (!IsAtEnd() && GetSeconds(ReplayedFrames) <= PlayedSeconds)
		{
			ReplayNextFrame();
		}
		bPlaying = !IsAtEnd();
	}

	void InputPlayback::SetPlaying(bool bPlay)
	{
		// At the end, Play starts again
		if (bPlay && IsAtEnd())
		{
			SeekTo(0);
		}
		bPlaying = bPlay && !IsAtEnd();
	}

	void InputPlayback::Step()
	{
		bPlaying = false;
		ReplayNextFrame();
		PlayedSeconds = ReplayedFrames > 0 ? GetSeconds(ReplayedFrames - 1) : 0.0;
	}

	void InputPlayback::SeekTo(size_t frames)
	{
		Replica = std::make_unique<InputReplica>();
		NextRecord = 0;
		ReplayedFrames = 0;
		while (ReplayedFrames < std::min(frames, FrameRecords.size()))
		{
			ReplayNextFrame();
		}
		PlayedSeconds = ReplayedFrames > 0 ? GetSeconds(ReplayedFrames - 1) : 0.0;
	}

	std::filesystem::path GetInputRecordingFolder()
	{
		return Paths::GetExecutableDir() / "InputRecordings";
	}

	std::filesystem::path MakeInputRecordingPath(const std::string& source)
	{
		// e.g. 2026-10-14_11-05-03-123
		const uint64_t timeMs = GetClockTimeMs();
		const std::time_t seconds = static_cast<std::time_t>(timeMs / 1000);
		std::tm local{};
		localtime_s(&local, &seconds);
		char stamp[40];
		std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d-%03u", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
			local.tm_hour, local.tm_min, local.tm_sec, static_cast<unsigned>(timeMs % 1000));

		// Characters a file name can't have
		std::string name = source;
		for (char& c : name)
		{
			c = std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos ? '_' : c;
		}
		return GetInputRecordingFolder() / ("Input_" + std::string(stamp) + "_" + name + ".nyxinputrec");
	}
}

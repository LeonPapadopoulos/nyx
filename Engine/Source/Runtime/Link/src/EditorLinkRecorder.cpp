#include "NyxPCH.h"
#include "EditorLinkRecorder.h"

#include "BinaryArchive.h"

#include <cstdio>

namespace
{
	using namespace Nyx::Engine;

	// "NXLL" in the file
	constexpr uint32_t RecordingMagic = 0x4C4C584E;
	constexpr uint32_t RecordingVersion = 1;

	void Indent(const std::string& text, std::string& outText)
	{
		size_t lineStart = 0;
		while (lineStart <= text.size())
		{
			const size_t lineEnd = text.find('\n', lineStart);
			const size_t end = lineEnd == std::string::npos ? text.size() : lineEnd;
			outText += "    " + text.substr(lineStart, end - lineStart) + "\n";
			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}
	}
}

namespace Nyx::Engine
{
	bool EditorLinkRecorder::Open(const std::filesystem::path& path, const std::string& programName)
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

		BinaryWriter header;
		header.WriteUInt32(RecordingMagic);
		header.WriteUInt32(RecordingVersion);
		header.WriteString(programName);
		header.WriteUInt64(GetClockTimeMs());
		File.write(reinterpret_cast<const char*>(header.GetBytes().data()), static_cast<std::streamsize>(header.GetBytes().size()));
		File.flush();

		Path = path;
		Start = std::chrono::steady_clock::now();
		return File.good();
	}

	void EditorLinkRecorder::Record(ELinkDirection direction, const Net::Message& message)
	{
		if (!File.is_open())
		{
			return;
		}

		const auto sinceStart = std::chrono::steady_clock::now() - Start;

		BinaryWriter record;
		record.WriteUInt64(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(sinceStart).count()));
		record.WriteUInt8(direction == ELinkDirection::Sent ? 0 : 1);
		record.WriteUInt16(message.Type);
		record.WriteUInt32(static_cast<uint32_t>(message.Payload.size()));
		record.WriteBytes(message.Payload.data(), message.Payload.size());

		File.write(reinterpret_cast<const char*>(record.GetBytes().data()), static_cast<std::streamsize>(record.GetBytes().size()));
		File.flush();
	}

	void EditorLinkRecorder::Close()
	{
		if (File.is_open())
		{
			File.close();
		}
		Path.clear();
	}

	bool EditorLinkRecorder::PrintFile(const std::filesystem::path& path, std::string& outText)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			outText += "Can't open '" + path.string() + "'.\n";
			return false;
		}

		uint32_t magic = 0;
		uint32_t version = 0;
		std::string programName;
		uint64_t startMs = 0;
		if (!reader.ReadUInt32(magic) || magic != RecordingMagic)
		{
			outText += "'" + path.string() + "' is not an editor link recording.\n";
			return false;
		}

		if (!reader.ReadUInt32(version))
		{
			outText += "'" + path.string() + "' ends early.\n";
			return false;
		}

		if (version != RecordingVersion)
		{
			outText += "'" + path.string() + "' is an editor link recording of version " + std::to_string(version) +
				", but this build reads version " + std::to_string(RecordingVersion) + ".\n";
			return false;
		}

		if (!reader.ReadString(programName) || !reader.ReadUInt64(startMs))
		{
			outText += "'" + path.string() + "' ends early.\n";
			return false;
		}

		outText += "Editor link recording '" + path.string() + "'\n";
		outText += "Recorded by " + programName + ", starting " + FormatClockTime(startMs, true) + ".\n";
		outText += "\"sent\" means sent by " + programName + ".\n";

		size_t messageCount = 0;
		while (reader.GetRemainingSize() > 0)
		{
			uint64_t timeUs = 0;
			uint8_t direction = 0;
			Net::Message message;
			uint32_t payloadSize = 0;
			if (!reader.ReadUInt64(timeUs) || !reader.ReadUInt8(direction) || !reader.ReadUInt16(message.Type) ||
				!reader.ReadUInt32(payloadSize) || payloadSize > reader.GetRemainingSize())
			{
				outText += "\nThe recording ends in the middle of a message here; the program may have ended while writing it.\n";
				return true;
			}

			message.Payload.resize(payloadSize);
			if (payloadSize > 0)
			{
				reader.ReadBytes(message.Payload.data(), payloadSize);
			}

			const EditorLinkMessageText text = DescribeEditorLinkMessage(message);

			char timeText[32];
			std::snprintf(timeText, sizeof(timeText), "+%.3f s", static_cast<double>(timeUs) / 1000000.0);

			outText += "\n" + std::string(timeText) + "  " + (direction == 0 ? "sent    " : "received") + "  " + text.Name + " (" +
				std::to_string(payloadSize) + " bytes)";
			if (!text.Summary.empty())
			{
				outText += ": " + text.Summary;
			}
			outText += "\n";
			Indent(text.Details, outText);
			++messageCount;
		}

		outText += "\n" + std::to_string(messageCount) + " messages\n";
		return true;
	}
}

#include "EditorLinkRecordingPrinter.h"

#include "BinaryArchive.h"
#include "EditorLinkMessageText.h"
#include "EditorLinkRecorder.h"

#include <cstdio>

namespace
{
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

namespace Nyx
{
	bool PrintEditorLinkRecording(const std::filesystem::path& path, std::string& outText)
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
		if (!reader.ReadUInt32(magic) || magic != EditorLinkRecorder::FileMagic)
		{
			outText += "'" + path.string() + "' is not an editor link recording.\n";
			return false;
		}

		if (!reader.ReadUInt32(version))
		{
			outText += "'" + path.string() + "' ends early.\n";
			return false;
		}

		if (version != EditorLinkRecorder::FileVersion)
		{
			outText += "'" + path.string() + "' is an editor link recording of version " + std::to_string(version) +
				", but this build reads version " + std::to_string(EditorLinkRecorder::FileVersion) + ".\n";
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

#include "NyxPCH.h"
#include "EditorLinkMessages.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace
{
	using namespace Nyx::Engine;

	template <typename TMessage>
	bool ReadPayload(const Nyx::Net::Message& message, TMessage& outMessage)
	{
		BinaryReader reader;
		reader.LoadFromMemory(message.Payload);
		return outMessage.Read(reader);
	}

	// The first bytes in hex, e.g. "0A 1F 00 ... (300 bytes)"
	std::string ToHex(const std::vector<std::byte>& bytes)
	{
		constexpr size_t MaxShown = 64;
		std::string text;
		char hex[4];
		for (size_t i = 0; i < bytes.size() && i < MaxShown; ++i)
		{
			std::snprintf(hex, sizeof(hex), "%02X ", static_cast<unsigned>(bytes[i]));
			text += hex;
		}

		if (bytes.size() > MaxShown)
		{
			text += "... ";
		}

		return text + "(" + std::to_string(bytes.size()) + " bytes)";
	}

	// For summaries, which are one line
	std::string OnOneLine(std::string text)
	{
		for (char& c : text)
		{
			if (c == '\n' || c == '\r')
			{
				c = ' ';
			}
		}
		return text;
	}

	std::string Quoted(const std::string& text)
	{
		return "\"" + text + "\"";
	}
}

namespace Nyx::Engine
{
	void HelloMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(ProtocolVersion);
		writer.WriteString(ProgramName);
		writer.WriteUInt32(ProcessId);
	}

	bool HelloMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt32(ProtocolVersion) && reader.ReadString(ProgramName) && reader.ReadUInt32(ProcessId);
	}

	const char* GetLogLevelName(ELogLevel level)
	{
		switch (level)
		{
		case ELogLevel::Trace: return "Trace";
		case ELogLevel::Debug: return "Debug";
		case ELogLevel::Info: return "Info";
		case ELogLevel::Warning: return "Warning";
		case ELogLevel::Error: return "Error";
		case ELogLevel::Critical: return "Critical";
		}
		return "Unknown level";
	}

	void LogLineMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt8(static_cast<uint8_t>(Level));
		writer.WriteUInt64(TimeMs);
		writer.WriteString(LoggerName);
		writer.WriteString(Text);
	}

	bool LogLineMessage::Read(BinaryReader& reader)
	{
		uint8_t level = 0;
		if (!reader.ReadUInt8(level) || !reader.ReadUInt64(TimeMs) || !reader.ReadString(LoggerName) || !reader.ReadString(Text))
		{
			return false;
		}

		Level = static_cast<ELogLevel>(level);
		return true;
	}

	EditorLinkMessageText DescribeEditorLinkMessage(const Net::Message& message)
	{
		EditorLinkMessageText text;

		const auto describeUnreadable = [&](const char* name)
		{
			text.Name = name;
			text.Summary = "can't be read (" + std::to_string(message.Payload.size()) + " bytes)";
			text.Details = "Payload: " + ToHex(message.Payload);
		};

		switch (static_cast<EEditorLinkMessage>(message.Type))
		{
		case EEditorLinkMessage::Hello:
		{
			HelloMessage hello;
			if (!ReadPayload(message, hello))
			{
				describeUnreadable("Hello");
				break;
			}

			text.Name = "Hello";
			text.Summary = hello.ProgramName + ", process " + std::to_string(hello.ProcessId) + ", protocol " +
				std::to_string(hello.ProtocolVersion);
			text.Details = "ProtocolVersion: " + std::to_string(hello.ProtocolVersion) + "\nProgramName: " +
				Quoted(hello.ProgramName) + "\nProcessId: " + std::to_string(hello.ProcessId);
			break;
		}

		case EEditorLinkMessage::Quit:
			text.Name = "Quit";
			text.Details = "(no fields)";
			break;

		case EEditorLinkMessage::LogLine:
		{
			LogLineMessage line;
			if (!ReadPayload(message, line))
			{
				describeUnreadable("LogLine");
				break;
			}

			text.Name = "LogLine";
			text.Summary = std::string("[") + GetLogLevelName(line.Level) + "] " + line.LoggerName + ": " + OnOneLine(line.Text);
			text.Details = std::string("Level: ") + GetLogLevelName(line.Level) + "\nTime: " + FormatClockTime(line.TimeMs) +
				"\nLogger: " + Quoted(line.LoggerName) + "\nText: " + Quoted(line.Text);
			break;
		}

		default:
			text.Name = "type " + std::to_string(message.Type);
			text.Summary = "a type this build doesn't know (" + std::to_string(message.Payload.size()) + " bytes)";
			text.Details = "Payload: " + ToHex(message.Payload);
			break;
		}

		return text;
	}

	std::string FormatClockTime(uint64_t timeMs, bool bWithDate)
	{
		const std::time_t seconds = static_cast<std::time_t>(timeMs / 1000);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &seconds);
#else
		localtime_r(&seconds, &local);
#endif

		char text[40];
		if (bWithDate)
		{
			std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d.%03u", local.tm_year + 1900, local.tm_mon + 1,
				local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, static_cast<unsigned>(timeMs % 1000));
		}
		else
		{
			std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03u", local.tm_hour, local.tm_min, local.tm_sec,
				static_cast<unsigned>(timeMs % 1000));
		}

		return text;
	}

	uint64_t GetClockTimeMs()
	{
		const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
	}
}

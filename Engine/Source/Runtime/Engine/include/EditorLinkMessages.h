#pragma once

#include "BinaryArchive.h"
#include "NetConnection.h"

#include <cstdint>
#include <string>

// The messages of the editor link. Each has a type number, and Write/Read for its payload.
// Adding a message: a number in EEditorLinkMessage, a struct like the ones below, and a case in
// DescribeEditorLinkMessage(), so the message log and NyxDump can show it.
namespace Nyx::Engine
{
	// Raise this whenever a message changes in a way the other side would misread. Both sides
	// refuse to talk to a different version, so an editor and a game built from different commits
	// fail with a clear error instead. New message types don't need it: a side that doesn't know
	// a type skips the message with a warning.
	constexpr uint32_t EditorLinkProtocolVersion = 1;

	// Message type numbers on the wire. Never reuse a number for something else.
	enum class EEditorLinkMessage : uint16_t
	{
		Hello = 1,
		Quit = 2,
		LogLine = 3,
	};

	// The first message in both directions. Its layout never changes, so any two versions can
	// read each other's Hello and tell that they differ.
	struct HelloMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::Hello;

		uint32_t ProtocolVersion = EditorLinkProtocolVersion;

		// "NyxEditor" or "NyxGame"
		std::string ProgramName;

		// Lets the editor check that the program that connected is the game it started
		uint32_t ProcessId = 0;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: please exit, as if the window was closed. The game closes the link itself.
	struct QuitMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::Quit;

		void Write(BinaryWriter& /*writer*/) const
		{
		}

		bool Read(BinaryReader& /*reader*/)
		{
			return true;
		}
	};

	// The same levels as the logger's, in the same order
	enum class ELogLevel : uint8_t
	{
		Trace,
		Debug,
		Info,
		Warning,
		Error,
		Critical,
	};

	const char* GetLogLevelName(ELogLevel level);

	// Game to editor: one line of the game's log
	struct LogLineMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::LogLine;

		ELogLevel Level = ELogLevel::Info;

		// When the game logged it: milliseconds since 1970 (UTC)
		uint64_t TimeMs = 0;

		// "ENGINE" or "APP"
		std::string LoggerName;

		std::string Text;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// A message as readable text, for the editor's message log and NyxDump
	struct EditorLinkMessageText
	{
		// "LogLine", or "type 57" for a type this build doesn't know
		std::string Name;

		// One line, e.g. "[Info] APP: Running scene 'Default.nyxscene'"
		std::string Summary;

		// Every field on its own line, e.g. "Level: Info"
		std::string Details;
	};

	EditorLinkMessageText DescribeEditorLinkMessage(const Net::Message& message);

	// "22:15:03.123", or "2026-10-09 22:15:03.123" with the date, in this computer's time zone.
	// timeMs is milliseconds since 1970 (UTC).
	std::string FormatClockTime(uint64_t timeMs, bool bWithDate = false);

	// Milliseconds since 1970 (UTC), for LogLineMessage::TimeMs
	uint64_t GetClockTimeMs();
}

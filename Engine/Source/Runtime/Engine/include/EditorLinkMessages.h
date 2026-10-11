#pragma once

#include "BinaryArchive.h"
#include "CrashHandler.h"
#include "EntityGuid.h"
#include "NetConnection.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The messages of the editor link. Each has a type number, and Write/Read for its payload.
// Adding a message: a number in EEditorLinkMessage, a struct like the ones below, and a case in
// DescribeEditorLinkMessage(), so the message log and NyxDump can show it.
//
// Messages name entities by guid, and components and properties by name (hash), never by handle
// or index: an editor and a game built from different commits then skip what only one of them
// knows instead of writing it into the wrong place.
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
		SetProperties = 4,
		CreateEntity = 5,
		DeleteEntity = 6,
		Crash = 7,
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

	// Live edits (see LiveEdits.h): the editor sends every edit, undo and redo to the game.

	// Editor to game: the values of some properties of one component of an entity
	struct SetPropertiesMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::SetProperties;

		EntityGuid Entity;

		// The component type's name, as in scene files, e.g. "Nyx::Engine::TransformComponent"
		std::string ComponentType;

		// Exactly one property block, with its size in front, as ReflectedArchiveSerializer::
		// SerializeProperties writes it: just the properties that are set. Written as it is.
		std::vector<std::byte> Properties;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: a whole entity with all its components, e.g. a new one, or one brought back
	// by undo. It replaces the entity with the same guid, if there is one.
	struct CreateEntityMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::CreateEntity;

		// The entity as in a scene file (SceneSerializer::WriteEntity): component count and
		// components. Its guid is in its GuidComponent. Sent with its size in front (u32).
		std::vector<std::byte> Entity;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: an entity was deleted
	struct DeleteEntityMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::DeleteEntity;

		EntityGuid Entity;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Game to editor, from the crash handler: the game is about to end because of an exception
	// nothing caught. The editor shows it on a crash card, with a Restart button.
	struct CrashMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::Crash;

		Nyx::CrashReport Report;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// A message as it travels over the connection, e.g. to keep it in a queue until a link is
	// connected (EditorLink::Send takes it as well)
	template <typename TMessage>
	Net::Message MakeNetMessage(const TMessage& message)
	{
		BinaryWriter writer;
		message.Write(writer);
		return Net::Message{ static_cast<uint16_t>(TMessage::Type), writer.GetBytes() };
	}

	// Reads a message's payload into the message type; false if it is cut off or damaged
	template <typename TMessage>
	bool ReadNetMessage(const Net::Message& message, TMessage& outMessage)
	{
		BinaryReader reader;
		reader.LoadFromMemory(message.Payload);
		return outMessage.Read(reader);
	}

	// A message as readable text, for the editor's message log and NyxDump
	struct EditorLinkMessageText
	{
		// "LogLine", or "type 57" for a type this build doesn't know
		std::string Name;

		// One line, e.g. "[Info] APP: Running scene 'Default.nyxscene'"
		std::string Summary;

		// Every field on its own line, e.g. "Level: Info". Left empty without bWithDetails.
		std::string Details;
	};

	// Without details, it is quicker for messages that carry a lot, such as CreateEntity, e.g.
	// for a log that only shows the details of the message selected.
	EditorLinkMessageText DescribeEditorLinkMessage(const Net::Message& message, bool bWithDetails = true);

	// "22:15:03.123", or "2026-10-09 22:15:03.123" with the date, in this computer's time zone.
	// timeMs is milliseconds since 1970 (UTC).
	std::string FormatClockTime(uint64_t timeMs, bool bWithDate = false);

	// Milliseconds since 1970 (UTC), for LogLineMessage::TimeMs
	uint64_t GetClockTimeMs();
}

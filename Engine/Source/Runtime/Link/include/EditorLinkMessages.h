#pragma once

#include "BinaryArchive.h"
#include "ClockTime.h"
#include "CrashHandler.h"
#include "EntityGuid.h"
#include "NetConnection.h"
#include "ShaderCompileTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The messages of the editor link. Each has a type number, and Write/Read for its payload.
// Adding a message: a number in EEditorLinkMessage, a struct like the ones below, and a case in
// DescribeEditorLinkMessage() (NyxLinkTools), so the message log and NyxDump can show it.
//
// Messages name entities by guid, and components and properties by name (hash), never by handle
// or index: an editor and a game built from different commits then skip what only one of them
// knows instead of writing it into the wrong place.
namespace Nyx
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
		ShaderProgram = 8,
		MaterialValues = 9,
		SetSimulationPaused = 10,
		StepSimulation = 11,
		SetInputAsset = 12,
		SetInputBackground = 13,
		InputSubscribe = 14,
		InputDefinitions = 15,
		InputFrame = 16,
		PhysicsMaterialValues = 17,
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

		// The component type's name, as in scene files, e.g. "Nyx::TransformComponent"
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
	// nothing caught
	struct CrashMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::Crash;

		Nyx::CrashReport Report;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Live materials: what the editor compiles and the values it edits reach the games it started;
	// see ApplyRendererEdit() in RendererLiveEdits.h.

	// Editor to game: a shader the editor compiled, the engine's or a material's: what was asked
	// (kind, file, domain) and what came out (each entry point's SPIR-V, a material's layout, the
	// files it read). Paths travel relative to the project's root, which both find themselves.
	struct ShaderProgramMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::ShaderProgram;

		ShaderCompileRequest Request;
		ShaderCompileResult Result;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: a material's values as the editor shows them, saved or not: the whole
	// .nyxmat text
	struct MaterialValuesMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::MaterialValues;

		// In the assets folder, e.g. "Materials/Stone.nyxmat"
		std::string Path;
		std::string Text;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: a physics material's values as the editor shows them, saved or not: the whole
	// .nyxphysmat text, for the game world's PhysicsMaterials (PhysicsMaterials::SetText())
	struct PhysicsMaterialValuesMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::PhysicsMaterialValues;

		// In the assets folder, e.g. "PhysicsMaterials/Wood.nyxphysmat"
		std::string Path;
		std::string Text;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Pause and Step: the editor's toolbar runs the games' simulations; see
	// ApplySimulationControl() in LiveEdits.h. Frames, the camera and live edits go on while paused.

	// Editor to game: stop or resume the fixed steps
	struct SetSimulationPausedMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::SetSimulationPaused;

		bool bPaused = false;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: while paused, run exactly one fixed step
	struct StepSimulationMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::StepSimulation;

		void Write(BinaryWriter& /*writer*/) const
		{
		}

		bool Read(BinaryReader& /*reader*/)
		{
			return true;
		}
	};

	// Live tuning (see LiveInput.h)

	// Editor to game: the whole input asset as the editor has it now, e.g. while a dead zone is
	// being dragged. The game puts it into its input system at once, with its player's settings
	// over it; held buttons stay held.
	struct SetInputAssetMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::SetInputAsset;

		// Exactly one property block of Nyx::Input::InputAssetData, with its size in front, as
		// ReflectedArchiveSerializer::SerializeObject writes it. Written as it is.
		std::vector<std::byte> Asset;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Editor to game: whether its gamepads keep driving it while none of its windows has the
	// focus, e.g. for every game of Play All at once (--input-background at startup)
	struct SetInputBackgroundMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::SetInputBackground;

		bool bGamepadsInBackground = false;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// The editor's view of a game's input (InputStream.h): the game streams its input frames while
	// the editor's Input window shows it

	// Editor to game: start or stop streaming the game's input
	struct InputSubscribeMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::InputSubscribe;

		bool bSubscribed = false;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Game to editor: every action and context of the game's input, when they changed. Record:
	// as Nyx::Input::WriteDefinitionsRecord writes it, with its size in front.
	struct InputDefinitionsMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::InputDefinitions;

		std::vector<std::byte> Record;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	// Game to editor: one frame of the game's input, as Nyx::Input::WriteFrameRecord writes it,
	// with its size in front
	struct InputFrameMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::InputFrame;

		std::vector<std::byte> Record;

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

	// A path under the project's root, made relative to it, as the other side finds its own root:
	// how shader messages carry paths
	std::filesystem::path ToLinkPath(const std::filesystem::path& path);
}

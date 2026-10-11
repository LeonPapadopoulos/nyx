#include "NyxPCH.h"
#include "EditorLinkMessages.h"

#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "NameComponent.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	// Bytes with their size in front (u32)
	void WriteSizedBytes(BinaryWriter& writer, const std::vector<std::byte>& bytes)
	{
		writer.WriteUInt32(static_cast<uint32_t>(bytes.size()));
		writer.WriteBytes(bytes.data(), bytes.size());
	}

	bool ReadSizedBytes(BinaryReader& reader, std::vector<std::byte>& outBytes)
	{
		uint32_t size = 0;
		if (!reader.ReadUInt32(size) || size > reader.GetRemainingSize())
		{
			return false;
		}

		outBytes.resize(size);
		return reader.ReadBytes(outBytes.data(), size);
	}

	// A block as BinaryWriter::WriteBlock wrote it, with its size, so ReadBlock can read it again
	bool ReadWholeBlock(BinaryReader& reader, std::vector<std::byte>& outBlock)
	{
		std::vector<std::byte> content;
		if (!ReadSizedBytes(reader, content))
		{
			return false;
		}

		BinaryWriter block;
		block.WriteUInt32(static_cast<uint32_t>(content.size()));
		block.WriteBytes(content.data(), content.size());
		outBlock = block.GetBytes();
		return true;
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

	std::string GuidToText(EntityGuid guid)
	{
		return ReflectedArchivePrinter::ValueToText(guid.Value);
	}

	// "TransformComponent" for "Nyx::Engine::TransformComponent", for summaries
	std::string ShortTypeName(const std::string& typeName)
	{
		const size_t lastColons = typeName.rfind("::");
		return lastColons == std::string::npos ? typeName : typeName.substr(lastColons + 2);
	}

	// Properties as ReflectedArchivePrinter prints them, on one line for a summary, e.g.
	// Position: (0, 2, 6), Mesh {Type: "Mesh", Path: "Cube"}
	std::string PrintedPropertiesOnOneLine(const std::string& printed)
	{
		// Each line with its nesting level: two spaces per level, counted from the first line
		struct PrintedLine
		{
			size_t Level = 0;
			std::string_view Content;
		};

		std::vector<PrintedLine> lines;
		size_t outerIndent = std::string::npos;

		size_t lineStart = 0;
		while (lineStart < printed.size())
		{
			size_t lineEnd = printed.find('\n', lineStart);
			if (lineEnd == std::string::npos)
			{
				lineEnd = printed.size();
			}

			const std::string_view line(printed.data() + lineStart, lineEnd - lineStart);
			lineStart = lineEnd + 1;

			const size_t indent = line.find_first_not_of(' ');
			if (indent == std::string_view::npos)
			{
				continue;
			}

			outerIndent = std::min(outerIndent, indent);
			lines.push_back({ indent > outerIndent ? (indent - outerIndent) / 2 : 0, line.substr(indent) });
		}

		std::string text;
		size_t openStructs = 0;

		for (size_t i = 0; i < lines.size(); ++i)
		{
			// Less indent than the line before ends the structs above it
			const bool bClosed = openStructs > lines[i].Level;
			for (; openStructs > lines[i].Level; --openStructs)
			{
				text += "}";
			}

			// Not right after a struct's opening brace
			if (i > 0 && (bClosed || lines[i - 1].Level >= lines[i].Level))
			{
				text += ", ";
			}

			// A struct is its name and a colon (or a note), with its properties indented below
			const std::string_view content = lines[i].Content;
			const std::string_view name = content.ends_with(':') ? content.substr(0, content.size() - 1) : content;
			const bool bOpensStruct = i + 1 < lines.size() && lines[i + 1].Level > lines[i].Level;

			if (bOpensStruct)
			{
				text += std::string(name) + " {";
				++openStructs;
			}
			else if (content.ends_with(':'))
			{
				text += std::string(name) + " {}";
			}
			else
			{
				text += std::string(content);
			}
		}

		for (; openStructs > 0; --openStructs)
		{
			text += "}";
		}

		return text;
	}

	const Nyx::Reflection::TypeMetadata* FindComponentType(const std::string& typeName)
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(typeName);
		return ops ? ops->TypeMetadata : nullptr;
	}

	// What a summary shows of an entity: guid and name, and its component types
	struct EntityOverview
	{
		EntityGuid Guid;
		std::string Name;
		bool bHasName = false;
		std::vector<std::string> ComponentTypes;
	};

	// Reads an entity as SceneSerializer::WriteEntity writes it, without a world to put it in
	bool ReadEntityOverview(const std::vector<std::byte>& entity, EntityOverview& outOverview)
	{
		const Nyx::Reflection::TypeMetadata& guidType = Nyx::Reflection::GetTypeMetadata<GuidComponent>();
		const Nyx::Reflection::TypeMetadata& nameType = Nyx::Reflection::GetTypeMetadata<NameComponent>();

		BinaryReader reader;
		reader.LoadFromMemory(entity);

		uint32_t componentCount = 0;
		if (!reader.ReadUInt32(componentCount))
		{
			return false;
		}

		for (uint32_t i = 0; i < componentCount; ++i)
		{
			std::string typeName;
			if (!reader.ReadString(typeName))
			{
				return false;
			}

			ReadWarnings ignored;
			bool bRead = false;

			if (typeName == guidType.Name)
			{
				GuidComponent guid;
				bRead = ReflectedArchiveSerializer::DeserializeObject(reader, &guid, guidType, ignored);
				outOverview.Guid = guid.Guid;
			}
			else if (typeName == nameType.Name)
			{
				NameComponent name;
				bRead = ReflectedArchiveSerializer::DeserializeObject(reader, &name, nameType, ignored);
				outOverview.Name = name.Name;
				outOverview.bHasName = true;
			}
			else
			{
				bRead = ReflectedArchiveSerializer::SkipObject(reader);
			}

			if (!bRead)
			{
				return false;
			}

			outOverview.ComponentTypes.push_back(std::move(typeName));
		}

		return true;
	}

	void DescribeSetProperties(const SetPropertiesMessage& message, bool bWithDetails, EditorLinkMessageText& outText)
	{
		const Nyx::Reflection::TypeMetadata* componentType = FindComponentType(message.ComponentType);

		BinaryReader reader;
		reader.LoadFromMemory(message.Properties);
		std::string printed;
		const bool bPrinted = ReflectedArchivePrinter::PrintObject(reader, componentType, 1, printed);

		outText.Summary = GuidToText(message.Entity) + " " + ShortTypeName(message.ComponentType) + ": " +
			PrintedPropertiesOnOneLine(printed) + (bPrinted ? "" : " (damaged)");

		if (bWithDetails)
		{
			outText.Details = "Entity: " + GuidToText(message.Entity) + "\nComponentType: " + Quoted(message.ComponentType) +
				(componentType ? "" : " (a component type this build doesn't know)") + "\nProperties:\n" + printed +
				(bPrinted ? "" : "  The properties end early or are damaged here.");
		}
	}

	void DescribeCreateEntity(const CreateEntityMessage& message, bool bWithDetails, EditorLinkMessageText& outText)
	{
		EntityOverview overview;
		const bool bReadable = ReadEntityOverview(message.Entity, overview);

		outText.Summary = GuidToText(overview.Guid);
		if (overview.bHasName)
		{
			outText.Summary += " " + Quoted(OnOneLine(overview.Name));
		}

		std::string components;
		for (const std::string& componentType : overview.ComponentTypes)
		{
			components += (components.empty() ? "" : ", ") + ShortTypeName(componentType);
		}

		if (!bReadable)
		{
			components += components.empty() ? "damaged" : ", damaged";
		}

		outText.Summary += " (" + components + ")";

		if (bWithDetails)
		{
			BinaryReader reader;
			reader.LoadFromMemory(message.Entity);
			outText.Details = "Entity: " + GuidToText(overview.Guid) + "\nComponents:\n";

			if (!SceneSerializer::PrintEntity(reader, 1, outText.Details))
			{
				outText.Details += "  The entity ends early or is damaged here.";
			}
		}
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

	void SetPropertiesMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt64(Entity.Value);
		writer.WriteString(ComponentType);

		// Already a block, with its size in front
		writer.WriteBytes(Properties.data(), Properties.size());
	}

	bool SetPropertiesMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt64(Entity.Value) && reader.ReadString(ComponentType) && ReadWholeBlock(reader, Properties);
	}

	void CreateEntityMessage::Write(BinaryWriter& writer) const
	{
		WriteSizedBytes(writer, Entity);
	}

	bool CreateEntityMessage::Read(BinaryReader& reader)
	{
		return ReadSizedBytes(reader, Entity);
	}

	void DeleteEntityMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt64(Entity.Value);
	}

	bool DeleteEntityMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt64(Entity.Value);
	}

	// A crash report has at most this many frames (the crash handler sends at most 64), so
	// damaged data can't make the reader allocate a lot
	constexpr uint32_t MaxCrashStackFrames = 256;

	void CrashMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(Report.ExceptionCode);
		writer.WriteString(Report.Description);

		const uint32_t frameCount = static_cast<uint32_t>((std::min)(Report.Frames.size(), static_cast<size_t>(MaxCrashStackFrames)));
		writer.WriteUInt32(frameCount);
		for (uint32_t i = 0; i < frameCount; ++i)
		{
			const CrashStackFrame& frame = Report.Frames[i];
			writer.WriteUInt64(frame.Address);
			writer.WriteString(frame.Module);
			writer.WriteString(frame.Function);
			writer.WriteString(frame.File);
			writer.WriteUInt32(frame.Line);
		}
	}

	bool CrashMessage::Read(BinaryReader& reader)
	{
		uint32_t frameCount = 0;
		if (!reader.ReadUInt32(Report.ExceptionCode) || !reader.ReadString(Report.Description) || !reader.ReadUInt32(frameCount) ||
			frameCount > MaxCrashStackFrames)
		{
			return false;
		}

		Report.Frames.resize(frameCount);
		for (CrashStackFrame& frame : Report.Frames)
		{
			if (!reader.ReadUInt64(frame.Address) || !reader.ReadString(frame.Module) || !reader.ReadString(frame.Function) ||
				!reader.ReadString(frame.File) || !reader.ReadUInt32(frame.Line))
			{
				return false;
			}
		}

		return true;
	}

	EditorLinkMessageText DescribeEditorLinkMessage(const Net::Message& message, bool bWithDetails)
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
			if (!ReadNetMessage(message, hello))
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
			if (!ReadNetMessage(message, line))
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

		case EEditorLinkMessage::SetProperties:
		{
			SetPropertiesMessage setProperties;
			if (!ReadNetMessage(message, setProperties))
			{
				describeUnreadable("SetProperties");
				break;
			}

			text.Name = "SetProperties";
			DescribeSetProperties(setProperties, bWithDetails, text);
			break;
		}

		case EEditorLinkMessage::CreateEntity:
		{
			CreateEntityMessage createEntity;
			if (!ReadNetMessage(message, createEntity))
			{
				describeUnreadable("CreateEntity");
				break;
			}

			text.Name = "CreateEntity";
			DescribeCreateEntity(createEntity, bWithDetails, text);
			break;
		}

		case EEditorLinkMessage::DeleteEntity:
		{
			DeleteEntityMessage deleteEntity;
			if (!ReadNetMessage(message, deleteEntity))
			{
				describeUnreadable("DeleteEntity");
				break;
			}

			text.Name = "DeleteEntity";
			text.Summary = GuidToText(deleteEntity.Entity);
			text.Details = "Entity: " + GuidToText(deleteEntity.Entity);
			break;
		}

		case EEditorLinkMessage::Crash:
		{
			CrashMessage crash;
			if (!ReadNetMessage(message, crash))
			{
				describeUnreadable("Crash");
				break;
			}

			text.Name = "Crash";
			text.Summary = crash.Report.Description + ", " + std::to_string(crash.Report.Frames.size()) + " frames";
			text.Details = FormatCrashReport(crash.Report);
			break;
		}

		default:
			text.Name = "type " + std::to_string(message.Type);
			text.Summary = "a type this build doesn't know (" + std::to_string(message.Payload.size()) + " bytes)";
			text.Details = "Payload: " + ToHex(message.Payload);
			break;
		}

		if (!bWithDetails)
		{
			text.Details.clear();
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

#include "EditorLinkMessageText.h"

#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "InputAsset.h"
#include "InputStream.h"
#include "NameComponent.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"
#include "ScenePrinter.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx;

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

	// "TransformComponent" for "Nyx::TransformComponent", for summaries
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
			const Nyx::Reflection::TypeMetadata* type = FindComponentType(typeName);

			if (type == &guidType)
			{
				GuidComponent guid;
				bRead = ReflectedArchiveSerializer::DeserializeObject(reader, &guid, guidType, ignored);
				outOverview.Guid = guid.Guid;
			}
			else if (type == &nameType)
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

	void DescribeInspectedEntity(const InspectedEntityMessage& message, bool bWithDetails, EditorLinkMessageText& outText)
	{
		std::string types;
		for (const InspectedComponent& component : message.Components)
		{
			types += (types.empty() ? "" : ", ") + ShortTypeName(component.Type);
		}
		outText.Summary = GuidToText(message.Entity) + (message.bFound ? " at step " + std::to_string(message.Step) + ": " +
			(types.empty() ? std::string("no run-time components") : types) : std::string(" not found"));

		if (!bWithDetails)
		{
			return;
		}

		outText.Details = "Request: " + std::to_string(message.Request) + "\nEntity: " + GuidToText(message.Entity) +
			"\nFound: " + (message.bFound ? "true" : "false") + "\nStep: " + std::to_string(message.Step);
		for (const InspectedComponent& component : message.Components)
		{
			const Nyx::Reflection::TypeMetadata* componentType = FindComponentType(component.Type);
			outText.Details += "\nComponent: " + Quoted(component.Type) + (componentType ? "" : " (a component type this build doesn't know)");
			if (!component.Properties.empty())
			{
				BinaryReader reader;
				reader.LoadFromMemory(component.Properties);
				std::string printed;
				const bool bPrinted = ReflectedArchivePrinter::PrintObject(reader, componentType, 1, printed);
				outText.Details += "\nProperties:\n" + printed + (bPrinted ? "" : "  The properties end early or are damaged here.");
			}
			for (const InspectedValue& value : component.Values)
			{
				outText.Details += "\n  " + value.Name + ": " + Quoted(value.Value);
			}
		}
	}

	void DescribeSetInputAsset(const SetInputAssetMessage& message, bool bWithDetails, EditorLinkMessageText& outText)
	{
		BinaryReader reader;
		reader.LoadFromMemory(message.Asset);
		Nyx::Input::InputAssetData asset;
		ReadWarnings warnings;
		const bool bReadable =
			ReflectedArchiveSerializer::DeserializeObject(reader, &asset, Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>(), warnings);

		outText.Summary = std::to_string(asset.Actions.size()) + " actions, " + std::to_string(asset.Contexts.size()) + " contexts, " +
			std::to_string(asset.PadProfiles.size()) + " pad profiles" + (bReadable ? "" : " (damaged)");

		if (bWithDetails)
		{
			BinaryReader printReader;
			printReader.LoadFromMemory(message.Asset);
			std::string printed;
			const bool bPrinted =
				ReflectedArchivePrinter::PrintObject(printReader, &Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>(), 1, printed);
			outText.Details = "Asset:\n" + printed + (bPrinted ? "" : "  The asset ends early or is damaged here.");
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

			if (!PrintSceneEntity(reader, 1, outText.Details))
			{
				outText.Details += "  The entity ends early or is damaged here.";
			}
		}
	}

	size_t SpirvBytes(const ShaderCompileResult& result)
	{
		size_t bytes = 0;
		for (const CompiledEntryPoint& entryPoint : result.EntryPoints)
		{
			bytes += entryPoint.Spirv.size() * sizeof(uint32_t);
		}
		return bytes;
	}
}

namespace Nyx
{
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

		case EEditorLinkMessage::ShaderProgram:
		{
			ShaderProgramMessage program;
			if (!ReadNetMessage(message, program))
			{
				describeUnreadable("ShaderProgram");
				break;
			}

			const char* kind = program.Request.Kind == EShaderCompileKind::Material ? "material" : program.Request.Kind == EShaderCompileKind::DefaultsWriter ? "material defaults"
																																							 : "program";
			text.Name = "ShaderProgram";
			text.Summary = ToLinkPath(program.Request.File).generic_string() + " (" + kind + "), " + std::to_string(program.Result.EntryPoints.size()) + " entry points, " +
				std::to_string(SpirvBytes(program.Result)) + " bytes of SPIR-V";
			text.Details = "File: " + Quoted(program.Request.File.string()) + "\nKind: " + kind + "\nDomain: " + Quoted(program.Request.Domain.Module);
			for (const CompiledEntryPoint& entryPoint : program.Result.EntryPoints)
			{
				text.Details += "\nEntry point: " + entryPoint.Name + ", " + std::to_string(entryPoint.Spirv.size() * sizeof(uint32_t)) + " bytes";
			}
			if (program.Result.MaterialLayout)
			{
				text.Details += "\nMaterial layout: " + program.Result.MaterialLayout->TypeName + ", " + std::to_string(program.Result.MaterialLayout->Fields.size()) + " fields, " +
					std::to_string(program.Result.MaterialLayout->Size) + " bytes";
			}
			break;
		}

		case EEditorLinkMessage::MaterialValues:
		{
			MaterialValuesMessage values;
			if (!ReadNetMessage(message, values))
			{
				describeUnreadable("MaterialValues");
				break;
			}

			const size_t lines = static_cast<size_t>(std::count(values.Text.begin(), values.Text.end(), '\n'));
			text.Name = "MaterialValues";
			text.Summary = values.Path + ", " + std::to_string(lines) + " lines";
			text.Details = "Path: " + Quoted(values.Path) + "\nText: " + Quoted(values.Text);
			break;
		}

		case EEditorLinkMessage::AssetValues:
		{
			AssetValuesMessage values;
			if (!ReadNetMessage(message, values))
			{
				describeUnreadable("AssetValues");
				break;
			}

			const size_t lines = static_cast<size_t>(std::count(values.Text.begin(), values.Text.end(), '\n'));
			text.Name = "AssetValues";
			text.Summary = values.Path + ", " + std::to_string(lines) + " lines";
			text.Details = "Path: " + Quoted(values.Path) + "\nText: " + Quoted(values.Text);
			break;
		}

		case EEditorLinkMessage::SetSimulationPaused:
		{
			SetSimulationPausedMessage paused;
			if (!ReadNetMessage(message, paused))
			{
				describeUnreadable("SetSimulationPaused");
				break;
			}

			text.Name = "SetSimulationPaused";
			text.Summary = paused.bPaused ? "paused" : "running";
			text.Details = std::string("Paused: ") + (paused.bPaused ? "true" : "false");
			break;
		}

		case EEditorLinkMessage::StepSimulation:
			text.Name = "StepSimulation";
			text.Details = "(no fields)";
			break;

		case EEditorLinkMessage::SetInputAsset:
		{
			SetInputAssetMessage setInputAsset;
			if (!ReadNetMessage(message, setInputAsset))
			{
				describeUnreadable("SetInputAsset");
				break;
			}

			text.Name = "SetInputAsset";
			DescribeSetInputAsset(setInputAsset, bWithDetails, text);
			break;
		}

		case EEditorLinkMessage::InputSubscribe:
		{
			InputSubscribeMessage subscribe;
			if (!ReadNetMessage(message, subscribe))
			{
				describeUnreadable("InputSubscribe");
				break;
			}

			text.Name = "InputSubscribe";
			text.Summary = subscribe.bSubscribed ? "start streaming the game's input" : "stop streaming the game's input";
			text.Details = std::string("Subscribed: ") + (subscribe.bSubscribed ? "true" : "false");
			break;
		}

		case EEditorLinkMessage::InputDefinitions:
		{
			InputDefinitionsMessage definitions;
			Nyx::Input::InputDefinitionsRecord record;
			BinaryReader reader;
			if (ReadNetMessage(message, definitions))
			{
				reader.LoadFromMemory(definitions.Record);
			}
			if (definitions.Record.empty() || !Nyx::Input::ReadDefinitionsRecord(reader, record))
			{
				describeUnreadable("InputDefinitions");
				break;
			}

			text.Name = "InputDefinitions";
			text.Summary = std::to_string(record.Actions.size()) + " actions, " + std::to_string(record.Contexts.size()) + " contexts";
			for (const Nyx::Input::InputActionDef& action : record.Actions)
			{
				text.Details += "Action: " + action.Name + " (" + Nyx::Reflection::EnumValueToText(Nyx::Reflection::GetEnumMetadata<Nyx::Input::EActionValue>(), static_cast<int64_t>(action.ValueType)) + ")\n";
			}
			for (const Nyx::Input::InputContextDef& context : record.Contexts)
			{
				text.Details += "Context: " + context.Name + " (" + std::to_string(context.Priority) + "), " +
					std::to_string(context.Bindings.size()) + " bindings\n";
			}
			break;
		}

		case EEditorLinkMessage::InputFrame:
		{
			InputFrameMessage frame;
			Nyx::Input::InputFrameRecord record;
			BinaryReader reader;
			if (ReadNetMessage(message, frame))
			{
				reader.LoadFromMemory(frame.Record);
			}
			if (frame.Record.empty() || !Nyx::Input::ReadFrameRecord(reader, record))
			{
				describeUnreadable("InputFrame");
				break;
			}

			text.Name = "InputFrame";
			text.Summary = std::to_string(record.Events.size()) + " events" + (record.Stack ? ", the stack" : "") +
				(record.Outputs.empty() ? "" : ", " + std::to_string(record.Outputs.size()) + " outputs");
			text.Details = "TimeUs: " + std::to_string(record.TimeUs) + "\n";
			for (const Nyx::Input::InputEvent& event : record.Events)
			{
				text.Details += "Event: " + std::to_string(event.TimeUs) + " device " + std::to_string(event.Device.Id.Value) + " " +
					(event.Kind == Nyx::Input::EInputEventKind::Control ? Nyx::Input::GetControlPath(event.Control) + " " +
								Nyx::Input::FormatValue(event.Value)
																		: std::string("kind ") + std::to_string(static_cast<int>(event.Kind))) +
					"\n";
			}
			if (record.Stack)
			{
				for (const Nyx::Input::InputStackRecord& entry : *record.Stack)
				{
					text.Details += "Stack: " + entry.Context + (entry.Reason.empty() ? "" : " (" + entry.Reason + ")") + "\n";
				}
			}
			break;
		}

		case EEditorLinkMessage::SetInputBackground:
		{
			SetInputBackgroundMessage background;
			if (!ReadNetMessage(message, background))
			{
				describeUnreadable("SetInputBackground");
				break;
			}

			text.Name = "SetInputBackground";
			text.Summary = background.bGamepadsInBackground ? "pads drive the game in the background" : "pads only with the focus";
			text.Details = std::string("GamepadsInBackground: ") + (background.bGamepadsInBackground ? "true" : "false");
			break;
		}

		case EEditorLinkMessage::InspectEntity:
		{
			InspectEntityMessage inspect;
			if (!ReadNetMessage(message, inspect))
			{
				describeUnreadable("InspectEntity");
				break;
			}

			text.Name = "InspectEntity";
			text.Summary = GuidToText(inspect.Entity);
			text.Details = "Request: " + std::to_string(inspect.Request) + "\nEntity: " + GuidToText(inspect.Entity);
			break;
		}

		case EEditorLinkMessage::InspectedEntity:
		{
			InspectedEntityMessage inspected;
			if (!ReadNetMessage(message, inspected))
			{
				describeUnreadable("InspectedEntity");
				break;
			}

			text.Name = "InspectedEntity";
			DescribeInspectedEntity(inspected, bWithDetails, text);
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
}

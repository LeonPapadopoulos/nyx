#include "InputFilePrinter.h"

#include "BinaryArchive.h"
#include "EditorLinkMessageText.h"
#include "InputAsset.h"
#include "InputRecording.h"
#include "InputTrace.h"
#include "ReflectedArchivePrinter.h"

#include <cstdio>

namespace
{
	using namespace Nyx::Input;

	std::string Seconds(uint64_t fromUs, uint64_t toUs)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.3f s", toUs >= fromUs ? static_cast<double>(toUs - fromUs) / 1'000'000.0 : 0.0);
		return text;
	}

	const char* EventKindName(EInputEventKind kind)
	{
		switch (kind)
		{
		case EInputEventKind::DeviceAdded:   return "added";
		case EInputEventKind::DeviceRemoved: return "removed";
		case EInputEventKind::DeviceChanged: return "changed";
		case EInputEventKind::Control:       return "control";
		case EInputEventKind::FocusChanged:  return "focus";
		}
		return "unknown";
	}

	std::string DescribeEvent(const InputEvent& event)
	{
		switch (event.Kind)
		{
		case EInputEventKind::DeviceAdded:
		case EInputEventKind::DeviceChanged:
			return std::string(EventKindName(event.Kind)) + " device " + std::to_string(event.Device.Id.Value) + " \"" + event.Device.Name +
				"\" (" + GetDeviceKindName(event.Device.Kind) + ", " + event.Device.Backend + ")";
		case EInputEventKind::DeviceRemoved:
			return "removed device " + std::to_string(event.Device.Id.Value);
		case EInputEventKind::Control:
			return "device " + std::to_string(event.Device.Id.Value) + " " + GetControlPath(event.Control) + " " +
				(IsButton(event.Control) ? std::string(event.Value.x != 0.0f ? "down" : "up") : FormatValue(event.Value));
		case EInputEventKind::FocusChanged:
			return event.bFocused ? "focus gained" : "focus lost";
		}
		return "unknown event";
	}
}

namespace Nyx
{
	bool PrintInputFile(const std::filesystem::path& path, std::string& outText)
	{
		BinaryReader reader;
		uint32_t magic = 0;
		uint32_t version = 0;
		if (!reader.LoadFromFile(path) || !reader.ReadUInt32(magic) || !reader.ReadUInt32(version))
		{
			outText += "Can't read the file\n";
			return false;
		}

		const Reflection::TypeMetadata* type = nullptr;
		if (magic == Input::InputAssetFileMagic)
		{
			type = &Reflection::GetTypeMetadata<Input::InputAssetData>();
			outText += "Input asset";
		}
		else if (magic == Input::InputPlayerSettingsFileMagic)
		{
			type = &Reflection::GetTypeMetadata<Input::InputPlayerSettingsData>();
			outText += "Player input settings";
		}
		else
		{
			outText += "Not an input asset or player input settings file\n";
			return false;
		}

		outText += ", version " + std::to_string(version) + "\n";
		if (!ReflectedArchivePrinter::PrintObject(reader, type, 1, outText))
		{
			outText += "The file is cut off or damaged\n";
			return false;
		}
		return true;
	}

	bool PrintInputRecording(const std::filesystem::path& path, std::string& outText)
	{
		std::string problem;
		const std::optional<Input::InputRecording> recording = Input::LoadInputRecording(path, &problem);
		if (!recording)
		{
			outText += "Can't print: " + problem + ".\n";
			return false;
		}

		outText += "Input recording '" + path.string() + "'\n";
		outText += "Of " + recording->ProgramName + ", starting " + FormatClockTime(recording->StartTimeMs, true) + ", " +
			std::to_string(recording->GetFrameCount()) + " frames.\n";

		uint64_t firstUs = 0;
		size_t frameIndex = 0;
		for (const Input::InputStreamRecord& record : recording->Records)
		{
			BinaryReader reader;
			reader.LoadFromMemory(record.Bytes);
			if (record.bDefinitions)
			{
				Input::InputDefinitionsRecord definitions;
				if (!Input::ReadDefinitionsRecord(reader, definitions))
				{
					outText += "Definitions: damaged.\n";
					return false;
				}
				outText += "Definitions: " + std::to_string(definitions.Actions.size()) + " actions, " + std::to_string(definitions.Contexts.size()) +
					" contexts\n";
				for (const Input::InputContextDef& context : definitions.Contexts)
				{
					outText += "  " + context.Name + " (" + std::to_string(context.Priority) + "): " + std::to_string(context.Bindings.size()) + " bindings\n";
				}
				continue;
			}

			Input::InputFrameRecord frame;
			if (!Input::ReadFrameRecord(reader, frame))
			{
				outText += "Frame " + std::to_string(frameIndex) + ": damaged.\n";
				return false;
			}
			firstUs = frameIndex == 0 ? frame.TimeUs : firstUs;

			// Quiet frames on one line each
			std::string line = "Frame " + std::to_string(frameIndex++) + " at " + Seconds(firstUs, frame.TimeUs);
			if (frame.ImGui.bTypingText || frame.ImGui.bMouseOverUI)
			{
				line += std::string(", ImGui") + (frame.ImGui.bTypingText ? " typing" : "") + (frame.ImGui.bMouseOverUI ? " mouse over UI" : "");
			}
			outText += line + "\n";
			for (const Input::InputEvent& event : frame.Events)
			{
				outText += "  " + DescribeEvent(event) + "\n";
			}
			if (frame.Stack)
			{
				std::string stack;
				for (const Input::InputStackRecord& entry : *frame.Stack)
				{
					stack += (stack.empty() ? "" : ", ") + entry.Context;
				}
				outText += "  stack (lowest first): " + (stack.empty() ? std::string("nothing") : stack) + "\n";
			}
			for (const std::string& action : frame.NewlyConsumed)
			{
				outText += "  the code took " + action + "\n";
			}
			for (const Input::InputUser::OutputRecord& output : frame.Outputs)
			{
				outText += "  output to device " + std::to_string(output.Device.Value) + ": " + output.What +
					(output.bSent ? "" : " (not sent: " + output.Reason + ")") + "\n";
			}
		}
		return true;
	}
}

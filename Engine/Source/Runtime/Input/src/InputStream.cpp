#include "InputStream.h"

#include "BinaryArchive.h"
#include "ReflectedArchiveSerializer.h"

#include <algorithm>
#include <utility>

namespace Nyx::Input
{
	namespace
	{
		using Nyx::BinaryReader;
		using Nyx::BinaryWriter;

		// Counts in damaged data can't make the reader allocate much
		constexpr uint32_t MaxCount = 1u << 16;
		constexpr uint32_t NoIndex = 0xFFFFFFFFu;

		bool ReadCount(BinaryReader& reader, uint32_t& outCount)
		{
			return reader.ReadUInt32(outCount) && outCount <= MaxCount;
		}

		void WriteVec3(BinaryWriter& writer, const glm::vec3& value)
		{
			writer.WriteFloat(value.x);
			writer.WriteFloat(value.y);
			writer.WriteFloat(value.z);
		}

		bool ReadVec3(BinaryReader& reader, glm::vec3& outValue)
		{
			return reader.ReadFloat(outValue.x) && reader.ReadFloat(outValue.y) && reader.ReadFloat(outValue.z);
		}

		void WriteControl(BinaryWriter& writer, ControlId control)
		{
			writer.WriteUInt8(static_cast<uint8_t>(control.Device));
			writer.WriteUInt16(control.Index);
		}

		bool ReadControl(BinaryReader& reader, ControlId& outControl)
		{
			uint8_t device = 0;
			if (!reader.ReadUInt8(device) || !reader.ReadUInt16(outControl.Index))
			{
				return false;
			}
			outControl.Device = static_cast<EDeviceKind>(device);
			return true;
		}

		void WriteOptionalControl(BinaryWriter& writer, const std::optional<ControlId>& control)
		{
			writer.WriteBool(control.has_value());
			if (control)
			{
				WriteControl(writer, *control);
			}
		}

		bool ReadOptionalControl(BinaryReader& reader, std::optional<ControlId>& outControl)
		{
			bool bHas = false;
			if (!reader.ReadBool(bHas))
			{
				return false;
			}
			outControl.reset();
			if (bHas)
			{
				ControlId control;
				if (!ReadControl(reader, control))
				{
					return false;
				}
				outControl = control;
			}
			return true;
		}

		uint32_t IndexOf(ActionHandle action)
		{
			return action.IsValid() ? action.Index() : NoIndex;
		}

		ActionHandle ActionAt(uint32_t index)
		{
			return index == NoIndex ? ActionHandle{} : ActionHandle::Make(index, 1);
		}

		template<typename T>
		void WriteReflected(BinaryWriter& writer, const T& object)
		{
			Nyx::ReflectedArchiveSerializer::SerializeObject(writer, &object, Reflection::GetTypeMetadata<T>());
		}

		template<typename T>
		bool ReadReflected(BinaryReader& reader, T& outObject)
		{
			Nyx::ReadWarnings warnings;
			return Nyx::ReflectedArchiveSerializer::DeserializeObject(reader, &outObject, Reflection::GetTypeMetadata<T>(), warnings);
		}

		void WriteDeviceInfo(BinaryWriter& writer, const InputDeviceInfo& info)
		{
			writer.WriteUInt32(info.Id.Value);
			writer.WriteUInt8(static_cast<uint8_t>(info.Kind));
			writer.WriteString(info.Name);
			writer.WriteString(info.Backend);
			writer.WriteUInt8(static_cast<uint8_t>(info.Model));
			writer.WriteUInt8(static_cast<uint8_t>(info.Connection));
			writer.WriteUInt32(static_cast<uint32_t>(info.Capabilities));
			writer.WriteBool(info.BatteryPercent.has_value());
			writer.WriteInt32(info.BatteryPercent.value_or(0));
			writer.WriteString(info.StableKey);
		}

		bool ReadDeviceInfo(BinaryReader& reader, InputDeviceInfo& outInfo)
		{
			uint8_t kind = 0;
			uint8_t model = 0;
			uint8_t connection = 0;
			uint32_t capabilities = 0;
			bool bBattery = false;
			int32_t battery = 0;
			if (!reader.ReadUInt32(outInfo.Id.Value) || !reader.ReadUInt8(kind) || !reader.ReadString(outInfo.Name) ||
				!reader.ReadString(outInfo.Backend) || !reader.ReadUInt8(model) || !reader.ReadUInt8(connection) ||
				!reader.ReadUInt32(capabilities) || !reader.ReadBool(bBattery) || !reader.ReadInt32(battery) || !reader.ReadString(outInfo.StableKey))
			{
				return false;
			}
			outInfo.Kind = static_cast<EDeviceKind>(kind);
			outInfo.Model = static_cast<EGamepadModel>(model);
			outInfo.Connection = static_cast<EConnection>(connection);
			outInfo.Capabilities = static_cast<EDeviceCapability>(capabilities);
			outInfo.BatteryPercent = bBattery ? std::optional<int>(battery) : std::nullopt;
			return true;
		}

		void WriteEvent(BinaryWriter& writer, const InputEvent& event)
		{
			writer.WriteUInt8(static_cast<uint8_t>(event.Kind));
			writer.WriteUInt64(event.TimeUs);
			WriteDeviceInfo(writer, event.Device);
			WriteControl(writer, event.Control);
			WriteVec3(writer, event.Value);
			writer.WriteBool(event.bFocused);
		}

		bool ReadEvent(BinaryReader& reader, InputEvent& outEvent)
		{
			uint8_t kind = 0;
			if (!reader.ReadUInt8(kind) || !reader.ReadUInt64(outEvent.TimeUs) || !ReadDeviceInfo(reader, outEvent.Device) ||
				!ReadControl(reader, outEvent.Control) || !ReadVec3(reader, outEvent.Value) || !reader.ReadBool(outEvent.bFocused))
			{
				return false;
			}
			outEvent.Kind = static_cast<EInputEventKind>(kind);
			return true;
		}

		void WriteBinding(BinaryWriter& writer, const InputBinding& binding)
		{
			writer.WriteUInt32(IndexOf(binding.Action));
			WriteOptionalControl(writer, binding.Control);
			writer.WriteInt32(binding.Swizzle.x);
			writer.WriteInt32(binding.Swizzle.y);
			writer.WriteInt32(binding.Swizzle.z);

			writer.WriteBool(binding.Composite.has_value());
			if (binding.Composite)
			{
				const CompositeKeys& keys = *binding.Composite;
				for (const std::optional<ControlId>* part : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
				{
					WriteOptionalControl(writer, *part);
				}
				writer.WriteBool(keys.bNormalize);
			}

			writer.WriteUInt8(static_cast<uint8_t>(binding.Modifiers));
			WriteReflected(writer, binding.Pipeline);
			WriteReflected(writer, binding.Trigger);
			writer.WriteUInt32(IndexOf(binding.Trigger.ChordAction));
			writer.WriteBool(binding.bTakesControls);
		}

		bool ReadBinding(BinaryReader& reader, InputBinding& outBinding)
		{
			uint32_t action = 0;
			bool bComposite = false;
			if (!reader.ReadUInt32(action) || !ReadOptionalControl(reader, outBinding.Control) || !reader.ReadInt32(outBinding.Swizzle.x) ||
				!reader.ReadInt32(outBinding.Swizzle.y) || !reader.ReadInt32(outBinding.Swizzle.z) || !reader.ReadBool(bComposite))
			{
				return false;
			}
			outBinding.Action = ActionAt(action);

			outBinding.Composite.reset();
			if (bComposite)
			{
				CompositeKeys keys;
				for (std::optional<ControlId>* part : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
				{
					if (!ReadOptionalControl(reader, *part))
					{
						return false;
					}
				}
				if (!reader.ReadBool(keys.bNormalize))
				{
					return false;
				}
				outBinding.Composite = keys;
			}

			uint8_t modifiers = 0;
			uint32_t chord = 0;
			if (!reader.ReadUInt8(modifiers) || !ReadReflected(reader, outBinding.Pipeline) || !ReadReflected(reader, outBinding.Trigger) ||
				!reader.ReadUInt32(chord) || !reader.ReadBool(outBinding.bTakesControls))
			{
				return false;
			}
			outBinding.Modifiers = static_cast<EModifierKeys>(modifiers);
			outBinding.Trigger.ChordAction = ActionAt(chord);
			return true;
		}

		void WriteBindings(BinaryWriter& writer, const std::vector<InputBinding>& bindings)
		{
			writer.WriteUInt32(static_cast<uint32_t>(bindings.size()));
			for (const InputBinding& binding : bindings)
			{
				WriteBinding(writer, binding);
			}
		}

		bool ReadBindings(BinaryReader& reader, std::vector<InputBinding>& outBindings)
		{
			uint32_t count = 0;
			if (!ReadCount(reader, count))
			{
				return false;
			}
			outBindings.resize(count);
			for (InputBinding& binding : outBindings)
			{
				if (!ReadBinding(reader, binding))
				{
					return false;
				}
			}
			return true;
		}

		// The stack as a frame record has it: lowest first, without the system contexts
		std::vector<InputStackRecord> RecordStack(const InputSystem& input)
		{
			std::vector<InputStackRecord> stack;
			const std::vector<InputUser::StackEntry> entries = input.GetUser().GetStack();
			for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
			{
				if (entry->Context == input.GetUnfocusedContext() || entry->Context == input.GetImGuiContext())
				{
					continue;
				}
				stack.push_back({ input.GetContext(entry->Context).Name, entry->Activation.Reason, entry->Activation.TakesDevices });
			}
			return stack;
		}

		bool SameStack(const std::vector<InputStackRecord>& a, const std::vector<InputStackRecord>& b)
		{
			return std::equal(a.begin(), a.end(), b.begin(), b.end(),
				[](const InputStackRecord& x, const InputStackRecord& y)
				{
					return x.Context == y.Context && x.Reason == y.Reason && x.TakesDevices == y.TakesDevices;
				});
		}

		// The frame's devices and what they hold, as events, for a replica that starts now. The
		// focus and the devices come first (time 0); a held button at the time it went down, so
		// holds and taps keep their times. Still, the replica sees held buttons pressed in this
		// frame: only this first frame may differ from the original's.
		std::vector<InputEvent> SnapshotEvents(const InputFrame& frame)
		{
			std::vector<InputEvent> events;
			events.push_back({ .Kind = EInputEventKind::FocusChanged, .TimeUs = 0, .bFocused = frame.HasFocus() });
			for (const InputFrame::DeviceState& device : frame.GetDevices())
			{
				if (!device.bRemoved)
				{
					events.push_back({ .Kind = EInputEventKind::DeviceAdded, .TimeUs = 0, .Device = device.Info });
				}
			}
			for (const InputFrame::DeviceState& device : frame.GetDevices())
			{
				for (size_t index = 0; index < device.Controls.size() && !device.bRemoved; ++index)
				{
					const ControlState& state = device.Controls[index];
					if (state.Value == glm::vec3(0.0f))
					{
						continue;
					}
					const ControlId id{ device.Info.Kind, static_cast<uint16_t>(index) };
					InputEvent control{ .Kind = EInputEventKind::Control };
					control.TimeUs = IsButton(id) && state.LastPressUs != 0 ? state.LastPressUs : frame.GetTimeUs();
					control.Device.Id = device.Info.Id;
					control.Control = id;
					control.Value = state.Value;
					events.push_back(control);
				}
			}
			return events;
		}
	}

	void WriteFrameRecord(Nyx::BinaryWriter& writer, const InputFrameRecord& record)
	{
		writer.WriteUInt64(record.TimeUs);
		writer.WriteUInt32(static_cast<uint32_t>(record.Events.size()));
		for (const InputEvent& event : record.Events)
		{
			WriteEvent(writer, event);
		}
		writer.WriteBool(record.ImGui.bTypingText);
		writer.WriteBool(record.ImGui.bMouseOverUI);
		writer.WriteBool(record.bGamepadsInBackground);

		writer.WriteBool(record.Stack.has_value());
		if (record.Stack)
		{
			writer.WriteUInt32(static_cast<uint32_t>(record.Stack->size()));
			for (const InputStackRecord& entry : *record.Stack)
			{
				writer.WriteString(entry.Context);
				writer.WriteString(entry.Reason);
				writer.WriteBool(entry.TakesDevices.has_value());
				writer.WriteUInt32(entry.TakesDevices.value_or(0));
			}
		}

		writer.WriteUInt32(static_cast<uint32_t>(record.NewlyConsumed.size()));
		for (const std::string& action : record.NewlyConsumed)
		{
			writer.WriteString(action);
		}

		writer.WriteUInt32(static_cast<uint32_t>(record.Outputs.size()));
		for (const InputUser::OutputRecord& output : record.Outputs)
		{
			writer.WriteUInt32(output.Device.Value);
			writer.WriteString(output.What);
			writer.WriteBool(output.bSent);
			writer.WriteString(output.Reason);
		}
	}

	bool ReadFrameRecord(Nyx::BinaryReader& reader, InputFrameRecord& outRecord)
	{
		uint32_t count = 0;
		if (!reader.ReadUInt64(outRecord.TimeUs) || !ReadCount(reader, count))
		{
			return false;
		}
		outRecord.Events.resize(count);
		for (InputEvent& event : outRecord.Events)
		{
			if (!ReadEvent(reader, event))
			{
				return false;
			}
		}

		bool bStack = false;
		if (!reader.ReadBool(outRecord.ImGui.bTypingText) || !reader.ReadBool(outRecord.ImGui.bMouseOverUI) ||
			!reader.ReadBool(outRecord.bGamepadsInBackground) || !reader.ReadBool(bStack))
		{
			return false;
		}

		outRecord.Stack.reset();
		if (bStack)
		{
			if (!ReadCount(reader, count))
			{
				return false;
			}
			std::vector<InputStackRecord> stack(count);
			for (InputStackRecord& entry : stack)
			{
				bool bTakes = false;
				uint32_t takes = 0;
				if (!reader.ReadString(entry.Context) || !reader.ReadString(entry.Reason) || !reader.ReadBool(bTakes) || !reader.ReadUInt32(takes))
				{
					return false;
				}
				entry.TakesDevices = bTakes ? std::optional<uint32_t>(takes) : std::nullopt;
			}
			outRecord.Stack = std::move(stack);
		}

		if (!ReadCount(reader, count))
		{
			return false;
		}
		outRecord.NewlyConsumed.resize(count);
		for (std::string& action : outRecord.NewlyConsumed)
		{
			if (!reader.ReadString(action))
			{
				return false;
			}
		}

		if (!ReadCount(reader, count))
		{
			return false;
		}
		outRecord.Outputs.resize(count);
		for (InputUser::OutputRecord& output : outRecord.Outputs)
		{
			if (!reader.ReadUInt32(output.Device.Value) || !reader.ReadString(output.What) || !reader.ReadBool(output.bSent) ||
				!reader.ReadString(output.Reason))
			{
				return false;
			}
		}
		return true;
	}

	void WriteDefinitionsRecord(Nyx::BinaryWriter& writer, const InputDefinitionsRecord& record)
	{
		writer.WriteUInt32(static_cast<uint32_t>(record.Actions.size()));
		for (const InputActionDef& action : record.Actions)
		{
			writer.WriteString(action.Name);
			writer.WriteUInt8(static_cast<uint8_t>(action.ValueType));
		}

		writer.WriteUInt32(static_cast<uint32_t>(record.Contexts.size()));
		for (const InputContextDef& context : record.Contexts)
		{
			writer.WriteString(context.Name);
			writer.WriteInt32(context.Priority);
			WriteBindings(writer, context.Bindings);
			writer.WriteBool(context.Cursor.has_value());
			writer.WriteUInt8(static_cast<uint8_t>(context.Cursor.value_or(ECursorMode::Free)));
			writer.WriteUInt32(context.TakesDevices);
			writer.WriteUInt32(static_cast<uint32_t>(context.ModelVariants.size()));
			for (const InputContextDef::ModelBindings& variant : context.ModelVariants)
			{
				writer.WriteUInt8(static_cast<uint8_t>(variant.Model));
				WriteBindings(writer, variant.Bindings);
			}
		}
	}

	bool ReadDefinitionsRecord(Nyx::BinaryReader& reader, InputDefinitionsRecord& outRecord)
	{
		uint32_t count = 0;
		if (!ReadCount(reader, count))
		{
			return false;
		}
		outRecord.Actions.resize(count);
		for (InputActionDef& action : outRecord.Actions)
		{
			uint8_t type = 0;
			if (!reader.ReadString(action.Name) || !reader.ReadUInt8(type))
			{
				return false;
			}
			action.ValueType = static_cast<EActionValue>(type);
		}

		if (!ReadCount(reader, count))
		{
			return false;
		}
		outRecord.Contexts.resize(count);
		for (InputContextDef& context : outRecord.Contexts)
		{
			int32_t priority = 0;
			bool bCursor = false;
			uint8_t cursor = 0;
			uint32_t variants = 0;
			if (!reader.ReadString(context.Name) || !reader.ReadInt32(priority) || !ReadBindings(reader, context.Bindings) ||
				!reader.ReadBool(bCursor) || !reader.ReadUInt8(cursor) || !reader.ReadUInt32(context.TakesDevices) || !ReadCount(reader, variants))
			{
				return false;
			}
			context.Priority = priority;
			context.Cursor = bCursor ? std::optional<ECursorMode>(static_cast<ECursorMode>(cursor)) : std::nullopt;
			context.ModelVariants.resize(variants);
			for (InputContextDef::ModelBindings& variant : context.ModelVariants)
			{
				uint8_t model = 0;
				if (!reader.ReadUInt8(model) || !ReadBindings(reader, variant.Bindings))
				{
					return false;
				}
				variant.Model = static_cast<EGamepadModel>(model);
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------
	// Writer

	InputStreamWriter::InputStreamWriter(InputSystem& input)
		: Input(input)
	{
		Input.SetFrameObserver(
			[this](const InputSystem& system)
			{
				OnFrame(system);
			});
	}

	InputStreamWriter::~InputStreamWriter()
	{
		Input.SetFrameObserver({});
	}

	void InputStreamWriter::Start()
	{
		bStreaming = true;
		bSnapshotNext = true;
		Records.clear();
	}

	void InputStreamWriter::Stop()
	{
		bStreaming = false;
		Records.clear();
	}

	std::vector<InputStreamWriter::Record> InputStreamWriter::TakeRecords()
	{
		return std::exchange(Records, {});
	}

	void InputStreamWriter::OnFrame(const InputSystem& input)
	{
		if (!bStreaming)
		{
			return;
		}

		const bool bSnapshot = std::exchange(bSnapshotNext, false);
		if (bSnapshot || input.GetDefinitionsRevision() != DefinitionsRevision)
		{
			InputDefinitionsRecord definitions;
			for (uint32_t index = 0; index < static_cast<uint32_t>(input.GetActionCount()); ++index)
			{
				definitions.Actions.push_back(input.GetAction(ActionHandle::Make(index, 1)));
			}
			for (uint32_t index = 0; index < static_cast<uint32_t>(input.GetContextCount()); ++index)
			{
				definitions.Contexts.push_back(input.GetContext(ContextHandle::Make(index, 1)));
			}
			BinaryWriter writer;
			WriteDefinitionsRecord(writer, definitions);
			Records.push_back({ true, writer.GetBytes() });
			DefinitionsRevision = input.GetDefinitionsRevision();
		}

		const InputFrame& frame = input.GetFrame();
		const InputUser& user = input.GetUser();

		InputFrameRecord record;
		record.TimeUs = frame.GetTimeUs();
		record.Events = bSnapshot ? SnapshotEvents(frame) : frame.GetEvents();
		record.ImGui = input.GetImGuiState();
		record.bGamepadsInBackground = input.AreGamepadsInBackground();

		std::vector<InputStackRecord> stack = RecordStack(input);
		if (bSnapshot || !SameStack(stack, LastStack))
		{
			record.Stack = stack;
			LastStack = std::move(stack);
		}

		if (bSnapshot)
		{
			Consumed.assign(input.GetActionCount(), false);
		}
		Consumed.resize(input.GetActionCount(), false);
		for (uint32_t index = 0; index < static_cast<uint32_t>(input.GetActionCount()); ++index)
		{
			const ActionHandle action = ActionHandle::Make(index, 1);
			if (!Consumed[index] && user.WasEverConsumed(action))
			{
				Consumed[index] = true;
				record.NewlyConsumed.push_back(input.GetAction(action).Name);
			}
		}

		record.Outputs = user.GetOutputRecords();

		BinaryWriter writer;
		WriteFrameRecord(writer, record);
		Records.push_back({ false, writer.GetBytes() });
	}

	// ---------------------------------------------------------------------------------------
	// Replica

	InputReplica::InputReplica()
	{
		Input.AddBackend(Backend);
		Input.SetLogging(false);
	}

	bool InputReplica::ApplyDefinitions(const std::vector<std::byte>& bytes)
	{
		BinaryReader reader;
		reader.LoadFromMemory(bytes);
		InputDefinitionsRecord record;
		if (!ReadDefinitionsRecord(reader, record))
		{
			return false;
		}

		// Both input systems made their system contexts first: the indices line up
		for (uint32_t index = 0; index < static_cast<uint32_t>(record.Actions.size()); ++index)
		{
			if (index < Input.GetActionCount())
			{
				Input.ReplaceAction(ActionHandle::Make(index, 1), std::move(record.Actions[index]));
			}
			else
			{
				Input.AddAction(std::move(record.Actions[index]));
			}
		}
		for (uint32_t index = 0; index < static_cast<uint32_t>(record.Contexts.size()); ++index)
		{
			if (index < Input.GetContextCount())
			{
				Input.ReplaceContext(ContextHandle::Make(index, 1), std::move(record.Contexts[index]));
			}
			else
			{
				Input.AddContext(std::move(record.Contexts[index]));
			}
		}
		return true;
	}

	bool InputReplica::ApplyFrame(const std::vector<std::byte>& bytes)
	{
		BinaryReader reader;
		reader.LoadFromMemory(bytes);
		InputFrameRecord record;
		if (!ReadFrameRecord(reader, record))
		{
			return false;
		}

		for (const InputEvent& event : record.Events)
		{
			Backend.Push(event);
		}
		Input.SetImGuiState(record.ImGui);
		Input.SetGamepadsInBackground(record.bGamepadsInBackground);

		InputUser& user = Input.GetUser();
		if (record.Stack)
		{
			// Off, then on in the original's order, so contexts of equal priority stack alike
			for (uint32_t index = 0; index < static_cast<uint32_t>(Input.GetContextCount()); ++index)
			{
				const ContextHandle context = ContextHandle::Make(index, 1);
				if (context != Input.GetUnfocusedContext() && context != Input.GetImGuiContext())
				{
					user.SetContextActive(context, false);
				}
			}
			for (const InputStackRecord& entry : *record.Stack)
			{
				user.SetContextActive(Input.FindContext(entry.Context), true, { .Reason = entry.Reason, .TakesDevices = entry.TakesDevices });
			}
		}
		for (const std::string& action : record.NewlyConsumed)
		{
			user.ConsumeTriggered(Input.FindAction(action));
		}

		Input.BeginFrame(record.TimeUs);
		user.ReplaceOutputRecords(std::move(record.Outputs));
		History.Record(Input);
		Input.EndFrame();
		++FrameCount;
		return true;
	}
}

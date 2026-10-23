#include "InputUser.h"

#include "InputSystem.h"

#include <algorithm>
#include <array>

namespace Nyx::Input
{
	namespace
	{
		constexpr size_t DeviceKindCount = 2;

		// How far the mouse must move in a frame to count as used on purpose, in pixels
		constexpr float MouseUseDistance = 3.0f;

		bool IsHeldPhase(EActionPhase phase)
		{
			return phase == EActionPhase::Started || phase == EActionPhase::Ongoing || phase == EActionPhase::Triggered;
		}

		// Which phase decides an action when several bindings drive it
		int PhaseRank(EActionPhase phase)
		{
			switch (phase)
			{
			case EActionPhase::Triggered: return 5;
			case EActionPhase::Ongoing:   return 4;
			case EActionPhase::Started:   return 3;
			case EActionPhase::Completed: return 2;
			case EActionPhase::Canceled:  return 1;
			case EActionPhase::Idle:      return 0;
			}
			return 0;
		}

		bool ModifiersHeld(const InputFrame& frame, EModifierKeys modifiers)
		{
			const auto held = [&frame](EKey left, EKey right)
			{
				return frame.IsDown(Key(left)) || frame.IsDown(Key(right));
			};
			return (!HasModifier(modifiers, EModifierKeys::Ctrl) || held(EKey::LeftControl, EKey::RightControl)) &&
				(!HasModifier(modifiers, EModifierKeys::Shift) || held(EKey::LeftShift, EKey::RightShift)) &&
				(!HasModifier(modifiers, EModifierKeys::Alt) || held(EKey::LeftAlt, EKey::RightAlt)) &&
				(!HasModifier(modifiers, EModifierKeys::Super) || held(EKey::LeftSuper, EKey::RightSuper));
		}

		// The binding's controls; for keys making up an axis, with what each adds
		struct BindingControl
		{
			ControlId Control;
			glm::vec3 Direction{ 0.0f };
		};

		std::vector<BindingControl> GetControls(const InputBinding& binding)
		{
			std::vector<BindingControl> controls;
			if (binding.Control)
			{
				controls.push_back({ *binding.Control, glm::vec3(0.0f) });
			}
			if (binding.Composite)
			{
				const CompositeKeys& keys = *binding.Composite;
				const std::array<std::pair<const std::optional<ControlId>*, glm::vec3>, 6> parts = { {
					{ &keys.Right, { 1.0f, 0.0f, 0.0f } },
					{ &keys.Left, { -1.0f, 0.0f, 0.0f } },
					{ &keys.Up, { 0.0f, 1.0f, 0.0f } },
					{ &keys.Down, { 0.0f, -1.0f, 0.0f } },
					{ &keys.Forward, { 0.0f, 0.0f, 1.0f } },
					{ &keys.Back, { 0.0f, 0.0f, -1.0f } },
				} };
				for (const auto& [key, direction] : parts)
				{
					if (*key)
					{
						controls.push_back({ **key, direction });
					}
				}
			}
			return controls;
		}

		// The first device of the control's kind on which it does something
		InputDeviceId FindActiveDevice(const InputFrame& frame, ControlId control)
		{
			for (const InputFrame::DeviceState& device : frame.GetDevices())
			{
				const ControlState state = frame.GetState(device.Info.Id, control);
				if (state.Value != glm::vec2(0.0f) || state.Presses > 0 || state.Releases > 0)
				{
					return device.Info.Id;
				}
			}
			return InputDeviceId{};
		}

		// Taken controls, per device kind and control: the context that took it
		class TakenControls
		{
		public:
			TakenControls()
			{
				for (size_t kind = 0; kind < DeviceKindCount; ++kind)
				{
					By[kind].resize(GetControlCount(static_cast<EDeviceKind>(kind)));
				}
			}

			ContextHandle Get(ControlId control) const
			{
				const auto& kind = By[static_cast<size_t>(control.Device)];
				return control.Index < kind.size() ? kind[control.Index] : ContextHandle{};
			}

			void Take(ControlId control, ContextHandle context)
			{
				auto& kind = By[static_cast<size_t>(control.Device)];
				if (control.Index < kind.size() && !kind[control.Index].IsValid())
				{
					kind[control.Index] = context;
				}
			}

			void TakeDevices(uint32_t devices, ContextHandle context)
			{
				for (size_t kind = 0; kind < DeviceKindCount; ++kind)
				{
					if ((devices & DeviceBit(static_cast<EDeviceKind>(kind))) == 0)
					{
						continue;
					}
					for (ContextHandle& takenBy : By[kind])
					{
						takenBy = takenBy.IsValid() ? takenBy : context;
					}
				}
			}

		private:
			std::array<std::vector<ContextHandle>, DeviceKindCount> By;
		};
	}

	const ActionState& InputUser::Get(ActionHandle action) const
	{
		static const ActionState none;
		const uint32_t index = action.Index();
		return action.IsValid() && index < Actions.size() ? Actions[index] : none;
	}

	bool InputUser::ConsumeTriggered(ActionHandle action)
	{
		const uint32_t index = action.Index();
		if (!action.IsValid() || index >= Pending.size() || !Pending[index].bPending)
		{
			return false;
		}
		Pending[index].bPending = false;
		return true;
	}

	void InputUser::SetContextActive(ContextHandle context, bool bActive, ContextActivation activation)
	{
		if (!context.IsValid())
		{
			return;
		}

		const uint32_t index = context.Index();
		if (index >= Active.size())
		{
			Active.resize(index + 1);
		}

		if (!bActive)
		{
			Active[index].reset();
		}
		else if (Active[index])
		{
			Active[index]->Activation = std::move(activation);
		}
		else
		{
			Active[index] = ActiveContext{ std::move(activation), ++ActivationCount };
		}
	}

	bool InputUser::IsContextActive(ContextHandle context) const
	{
		return context.IsValid() && context.Index() < Active.size() && Active[context.Index()].has_value();
	}

	std::vector<InputUser::StackEntry> InputUser::GetStack() const
	{
		std::vector<uint32_t> indices;
		for (uint32_t index = 0; index < Active.size(); ++index)
		{
			if (Active[index])
			{
				indices.push_back(index);
			}
		}
		SortByStackOrder(indices);

		std::vector<StackEntry> stack;
		for (const uint32_t index : indices)
		{
			stack.push_back({ ContextHandle::Make(index, 1), Active[index]->Activation });
		}
		return stack;
	}

	std::vector<const BindingTrace*> InputUser::GetTraces(ActionHandle action) const
	{
		std::vector<const BindingTrace*> traces;
		for (const BindingTrace& trace : Traces)
		{
			if (trace.Action == action)
			{
				traces.push_back(&trace);
			}
		}
		return traces;
	}

	void InputUser::Evaluate(const InputSystem& input, const InputFrame& frame, float deltaSeconds)
	{
		const uint64_t now = frame.GetTimeUs();
		const size_t actionCount = input.GetActionCount();
		const size_t contextCount = input.GetContextCount();

		PreviousActions = Actions;
		PreviousActions.resize(actionCount);
		Actions.assign(actionCount, ActionState{});
		Pending.resize(actionCount);
		Active.resize(contextCount);
		Bindings.resize(contextCount);

		for (PendingTrigger& pending : Pending)
		{
			pending.bPending = pending.bPending && now <= pending.ExpiresUs;
		}

		UpdateLastUsedDevice(frame);

		// The stack, highest first
		std::vector<uint32_t> stack;
		for (uint32_t index = 0; index < contextCount; ++index)
		{
			Bindings[index].resize(input.GetContext(ContextHandle::Make(index, 1)).Bindings.size());
			if (Active[index])
			{
				stack.push_back(index);
			}
		}
		SortByStackOrder(stack);

		// Each binding of a context on the stack: what its controls give it, before the triggers
		struct Evaluation
		{
			size_t TraceIndex = 0;
			const InputBinding* Binding = nullptr;
			BindingRuntime* Runtime = nullptr;
			TriggerInput Input;
		};
		std::vector<Evaluation> evaluations;

		Traces.clear();
		TakenControls taken;

		for (const uint32_t contextIndex : stack)
		{
			const ContextHandle context = ContextHandle::Make(contextIndex, 1);
			const InputContextDef& def = input.GetContext(context);
			std::vector<ControlId> takes;

			for (uint32_t bindingIndex = 0; bindingIndex < def.Bindings.size(); ++bindingIndex)
			{
				const InputBinding& binding = def.Bindings[bindingIndex];
				BindingRuntime& runtime = Bindings[contextIndex][bindingIndex];

				BindingTrace trace;
				trace.Action = binding.Action;
				trace.Context = context;
				trace.BindingIndex = bindingIndex;

				const std::vector<BindingControl> controls = GetControls(binding);
				std::vector<const BindingControl*> available;
				for (const BindingControl& control : controls)
				{
					if (const ContextHandle takenBy = taken.Get(control.Control); takenBy.IsValid())
					{
						trace.TakenBy = trace.TakenBy.IsValid() ? trace.TakenBy : takenBy;
						trace.TakenControls.push_back(control.Control);
					}
					else
					{
						available.push_back(&control);
					}
				}

				Evaluation evaluation;
				evaluation.Binding = &binding;
				evaluation.Runtime = &runtime;
				evaluation.Input.NowUs = now;

				const bool bModifiersHeld = ModifiersHeld(frame, binding.Modifiers);
				const bool bSingleButton = binding.Control && IsButton(*binding.Control);
				glm::vec3 raw{ 0.0f };

				if (!bModifiersHeld)
				{
					trace.Outcome = EBindingOutcome::MissingModifiers;
				}
				else if (available.empty() && !controls.empty())
				{
					trace.Outcome = EBindingOutcome::Taken;
				}
				else if (binding.Control)
				{
					const ControlId control = *binding.Control;
					const ControlState state = frame.GetState(control);
					trace.Device = FindActiveDevice(frame, control);
					if (bSingleButton)
					{
						raw.x = state.Value.x;
						evaluation.Input.bDown = state.Value.x != 0.0f;
						evaluation.Input.Presses = state.Presses;
						evaluation.Input.Releases = state.Releases;
						evaluation.Input.PressUs = state.LastPressUs;
						evaluation.Input.ReleaseUs = state.LastReleaseUs;
						if (state.Presses > 0)
						{
							runtime.PressUs = state.LastPressUs;
						}
						else if (evaluation.Input.bDown && runtime.PressUs == 0)
						{
							// Held already when its context came on: held from now
							runtime.PressUs = now;
						}
					}
					else if (binding.SourceAxis >= 0 && binding.SourceAxis < 2)
					{
						raw.x = state.Value[binding.SourceAxis];
					}
					else
					{
						raw = glm::vec3(state.Value, 0.0f);
					}
				}
				else
				{
					for (const BindingControl* control : available)
					{
						if (frame.IsDown(control->Control))
						{
							raw += control->Direction;
							trace.Device = trace.Device.IsValid() ? trace.Device : FindActiveDevice(frame, control->Control);
						}
					}
					if (binding.Composite->bNormalize && glm::length(raw) > 0.0f)
					{
						raw = glm::normalize(raw);
					}
				}

				// Run even without input, so smoothing settles and look acceleration resets
				const PipelineResult result = RunPipeline(raw, binding.Pipeline, runtime.Pipeline, deltaSeconds);
				trace.Raw = raw;
				trace.AfterStep = result.AfterStep;
				trace.Value = result.Value;

				if (!bSingleButton)
				{
					// Analog: down from the press threshold, up below the release threshold
					const float magnitude = glm::length(result.Value);
					if (!runtime.bActuated && magnitude >= binding.Trigger.PressThreshold)
					{
						runtime.bActuated = true;
						runtime.PressUs = now;
						evaluation.Input.Presses = 1;
					}
					else if (runtime.bActuated && magnitude < binding.Trigger.ReleaseThreshold)
					{
						runtime.bActuated = false;
						evaluation.Input.Releases = 1;
						evaluation.Input.ReleaseUs = now;
					}
					evaluation.Input.bDown = runtime.bActuated;
					evaluation.Input.PressUs = runtime.PressUs;
				}

				// A binding takes its controls only while it can act: Ctrl+C doesn't take C
				if (binding.bTakesControls && bModifiersHeld)
				{
					for (const BindingControl* control : available)
					{
						takes.push_back(control->Control);
					}
				}

				evaluation.TraceIndex = Traces.size();
				Traces.push_back(std::move(trace));
				evaluations.push_back(evaluation);
			}

			// Within a context, bindings don't take from each other
			for (const ControlId control : takes)
			{
				taken.Take(control, context);
			}

			const ActiveContext& active = *Active[contextIndex];
			taken.TakeDevices(active.Activation.TakesDevices.value_or(def.TakesDevices), context);
		}

		// Contexts that are off: their bindings start afresh when they come on
		for (uint32_t contextIndex = 0; contextIndex < contextCount; ++contextIndex)
		{
			if (Active[contextIndex])
			{
				continue;
			}
			const ContextHandle context = ContextHandle::Make(contextIndex, 1);
			const InputContextDef& def = input.GetContext(context);
			for (uint32_t bindingIndex = 0; bindingIndex < def.Bindings.size(); ++bindingIndex)
			{
				Bindings[contextIndex][bindingIndex] = BindingRuntime{};

				BindingTrace trace;
				trace.Action = def.Bindings[bindingIndex].Action;
				trace.Context = context;
				trace.BindingIndex = bindingIndex;
				trace.Outcome = EBindingOutcome::ContextOff;
				Traces.push_back(std::move(trace));
			}
		}

		// The triggers, and from them the actions. Chords last: they ask whether another action
		// is triggered in this frame.
		std::vector<float> bufferSeconds(actionCount, 0.0f);
		std::vector<int> decidingRank(actionCount, -1);
		std::vector<float> decidingMagnitude(actionCount, -1.0f);
		std::vector<float> valueMagnitude(actionCount, 0.0f);

		const auto evaluate = [&](Evaluation& evaluation)
		{
			const InputBinding& binding = *evaluation.Binding;
			BindingTrace& trace = Traces[evaluation.TraceIndex];
			const uint32_t actionIndex = binding.Action.Index();
			if (!binding.Action.IsValid() || actionIndex >= actionCount)
			{
				return;
			}

			if (binding.Trigger.Kind == ETriggerKind::Chord)
			{
				evaluation.Input.bChordTriggered = Get(binding.Trigger.ChordAction).IsTriggered();
			}

			const TriggerResult result = UpdateTrigger(binding.Trigger, evaluation.Runtime->Trigger, evaluation.Input);
			trace.Trigger = result;

			const bool bButtonAction = input.GetAction(binding.Action).ValueType == EActionValue::Button;
			const bool bGivesValue = !bButtonAction && trace.Value != glm::vec3(0.0f);
			if (trace.Outcome != EBindingOutcome::MissingModifiers && trace.Outcome != EBindingOutcome::Taken)
			{
				if (result.Phase == EActionPhase::Canceled)
				{
					trace.Outcome = EBindingOutcome::Canceled;
				}
				else if (result.bTriggered || result.Phase == EActionPhase::Triggered || bGivesValue)
				{
					trace.Outcome = EBindingOutcome::Contributed;
				}
				else if (result.Phase == EActionPhase::Started || result.Phase == EActionPhase::Ongoing)
				{
					trace.Outcome = EBindingOutcome::Waiting;
				}
				else if (trace.Raw != glm::vec3(0.0f))
				{
					trace.Outcome = EBindingOutcome::Zeroed;
					for (size_t step = 0; step < PipelineStepCount; ++step)
					{
						if (trace.AfterStep[step] && *trace.AfterStep[step] == glm::vec3(0.0f))
						{
							trace.ZeroedBy = static_cast<EPipelineStep>(step);
							break;
						}
					}
				}
				else
				{
					trace.Outcome = EBindingOutcome::Idle;
				}
			}

			ActionState& action = Actions[actionIndex];
			if (result.bTriggered)
			{
				action.bTriggered = true;
				bufferSeconds[actionIndex] = std::max(bufferSeconds[actionIndex], binding.Trigger.BufferSeconds);
			}

			// The phase that ranks highest decides, and of equal ones the larger value
			const int rank = PhaseRank(result.Phase);
			const float magnitude = glm::length(trace.Value);
			if (rank > decidingRank[actionIndex] || (rank == decidingRank[actionIndex] && magnitude > decidingMagnitude[actionIndex]))
			{
				decidingRank[actionIndex] = rank;
				decidingMagnitude[actionIndex] = magnitude;
				action.Phase = result.Phase;
				action.Context = trace.Context;
				action.BindingIndex = trace.BindingIndex;
				action.Device = trace.Device;
				action.HeldSeconds = evaluation.Input.bDown && now > evaluation.Runtime->PressUs
					? static_cast<float>(now - evaluation.Runtime->PressUs) / 1'000'000.0f
					: 0.0f;
			}

			// Axes take the largest value of the bindings that weren't stopped
			if (!bButtonAction && trace.Outcome != EBindingOutcome::MissingModifiers && trace.Outcome != EBindingOutcome::Taken &&
				magnitude > valueMagnitude[actionIndex])
			{
				valueMagnitude[actionIndex] = magnitude;
				action.Value = trace.Value;
			}
		};

		for (Evaluation& evaluation : evaluations)
		{
			if (evaluation.Binding->Trigger.Kind != ETriggerKind::Chord)
			{
				evaluate(evaluation);
			}
		}
		for (Evaluation& evaluation : evaluations)
		{
			if (evaluation.Binding->Trigger.Kind == ETriggerKind::Chord)
			{
				evaluate(evaluation);
			}
		}

		for (uint32_t index = 0; index < actionCount; ++index)
		{
			ActionState& action = Actions[index];
			if (input.GetAction(ActionHandle::Make(index, 1)).ValueType == EActionValue::Button)
			{
				action.Value = glm::vec3(action.IsTriggered() ? 1.0f : 0.0f, 0.0f, 0.0f);
			}
			action.bStarted = IsHeldPhase(action.Phase) && !IsHeldPhase(PreviousActions[index].Phase);

			if (action.bTriggered)
			{
				Pending[index].bPending = true;
				Pending[index].ExpiresUs = now + static_cast<uint64_t>(bufferSeconds[index] * 1'000'000.0f);
			}
		}
	}

	void InputUser::DecideCursor()
	{
		Cursor = CursorRequest{};
		for (const StackEntry& entry : GetStack())
		{
			const InputContextDef& def = System->GetContext(entry.Context);
			if (def.Cursor)
			{
				Cursor = CursorRequest{ *def.Cursor, entry.Activation.Window, def.Name };
				return;
			}
		}
	}

	void InputUser::SortByStackOrder(std::vector<uint32_t>& contextIndices) const
	{
		std::stable_sort(contextIndices.begin(), contextIndices.end(),
			[this](uint32_t a, uint32_t b)
			{
				const int priorityA = System->GetContext(ContextHandle::Make(a, 1)).Priority;
				const int priorityB = System->GetContext(ContextHandle::Make(b, 1)).Priority;
				return priorityA != priorityB ? priorityA > priorityB : Active[a]->Order > Active[b]->Order;
			});
	}

	void InputUser::UpdateLastUsedDevice(const InputFrame& frame)
	{
		std::vector<std::pair<InputDeviceId, float>> mouseDistance;
		for (const InputEvent& event : frame.GetEvents())
		{
			if (event.Kind != EInputEventKind::Control)
			{
				continue;
			}

			if (event.Control == Mouse(EMouseControl::Delta))
			{
				auto found = std::find_if(mouseDistance.begin(), mouseDistance.end(),
					[&event](const auto& entry)
					{
						return entry.first == event.Device.Id;
					});
				if (found == mouseDistance.end())
				{
					mouseDistance.push_back({ event.Device.Id, 0.0f });
					found = std::prev(mouseDistance.end());
				}
				const bool bWasBelow = found->second < MouseUseDistance;
				found->second += glm::length(event.Value);
				if (bWasBelow && found->second >= MouseUseDistance)
				{
					LastUsedDevice = event.Device.Id;
				}
			}
			else if (IsRelative(event.Control) ? event.Value != glm::vec2(0.0f) : event.Value.x != 0.0f)
			{
				LastUsedDevice = event.Device.Id;
			}
		}
	}
}

#include "InputTrace.h"

#include "InputSystem.h"

#include <cstdio>

namespace Nyx::Input
{
	namespace
	{
		std::string FormatNumber(float value)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.3g", value == 0.0f ? 0.0f : value);
			return text;
		}

		std::string FormatSeconds(float seconds)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.2f s", seconds);
			return text;
		}

		std::string ContextLabel(const InputSystem& input, ContextHandle context)
		{
			const InputContextDef& def = input.GetContext(context);
			return def.Name + " (" + std::to_string(def.Priority) + ")";
		}

		std::string ModifierPrefix(EModifierKeys modifiers)
		{
			std::string prefix;
			prefix += HasModifier(modifiers, EModifierKeys::Ctrl) ? "Ctrl+" : "";
			prefix += HasModifier(modifiers, EModifierKeys::Alt) ? "Alt+" : "";
			prefix += HasModifier(modifiers, EModifierKeys::Shift) ? "Shift+" : "";
			prefix += HasModifier(modifiers, EModifierKeys::Super) ? "Super+" : "";
			return prefix;
		}
	}

	const char* GetBindingOutcomeName(EBindingOutcome outcome)
	{
		switch (outcome)
		{
		case EBindingOutcome::ContextOff:       return "context off";
		case EBindingOutcome::MissingModifiers: return "modifier keys not held";
		case EBindingOutcome::Taken:            return "taken";
		case EBindingOutcome::Idle:             return "idle";
		case EBindingOutcome::Zeroed:           return "zeroed";
		case EBindingOutcome::Waiting:          return "waiting";
		case EBindingOutcome::Canceled:         return "canceled";
		case EBindingOutcome::Contributed:      return "contributed";
		}
		return "unknown";
	}

	std::string FormatValue(const glm::vec3& value)
	{
		return "(" + FormatNumber(value.x) + ", " + FormatNumber(value.y) + ", " + FormatNumber(value.z) + ")";
	}

	std::string DescribeBindingSource(const InputBinding& binding)
	{
		std::string source = ModifierPrefix(binding.Modifiers);
		if (binding.Control)
		{
			source += GetControlPath(*binding.Control);
			if (binding.SourceAxis >= 0)
			{
				source += binding.SourceAxis == 0 ? ".x" : ".y";
			}
			return source;
		}

		if (binding.Composite)
		{
			const CompositeKeys& keys = *binding.Composite;
			bool bFirst = true;
			for (const std::optional<ControlId>* key : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
			{
				if (!*key)
				{
					continue;
				}
				const std::string path = GetControlPath(**key);
				// "Keyboard/D,A,W": the device once, then each control's name
				source += bFirst ? path : "," + path.substr(path.find('/') + 1);
				bFirst = false;
			}
		}
		return source;
	}

	std::string FormatTrace(const InputSystem& input, const BindingTrace& trace)
	{
		const InputContextDef& context = input.GetContext(trace.Context);
		const InputBinding* binding = trace.BindingIndex < context.Bindings.size() ? &context.Bindings[trace.BindingIndex] : nullptr;

		std::string text = input.GetAction(trace.Action).Name + "   " + ContextLabel(input, trace.Context);
		if (binding)
		{
			text += " - " + DescribeBindingSource(*binding) + " - " + GetTriggerKindName(binding->Trigger.Kind);
		}
		text += "\n";

		switch (trace.Outcome)
		{
		case EBindingOutcome::ContextOff:
			return text + "stopped: context off";
		case EBindingOutcome::MissingModifiers:
		{
			// "Ctrl+Alt+" without its last +
			const std::string keys = binding ? ModifierPrefix(binding->Modifiers) : std::string("modifier keys+");
			return text + "stopped: " + keys.substr(0, keys.size() - 1) + " not held";
		}
		case EBindingOutcome::Taken:
			return text + "stopped: taken by " + ContextLabel(input, trace.TakenBy);
		case EBindingOutcome::Idle:
			return text + "idle";
		default:
			break;
		}

		text += "raw " + FormatValue(trace.Raw);
		for (size_t step = 0; step < PipelineStepCount; ++step)
		{
			if (trace.AfterStep[step])
			{
				text += " -> " + std::string(GetPipelineStepName(static_cast<EPipelineStep>(step))) + " " + FormatValue(*trace.AfterStep[step]);
			}
		}

		const char* triggerName = binding ? GetTriggerKindName(binding->Trigger.Kind) : "trigger";
		switch (trace.Outcome)
		{
		case EBindingOutcome::Zeroed:
			text += " -> stopped: zeroed by " + std::string(GetPipelineStepName(trace.ZeroedBy));
			break;
		case EBindingOutcome::Waiting:
			text += " -> waiting: " + std::string(triggerName) + " at " + FormatSeconds(trace.Trigger.WaitedSeconds) + " of " +
				FormatSeconds(trace.Trigger.NeededSeconds);
			break;
		case EBindingOutcome::Canceled:
			text += " -> stopped: " + std::string(triggerName) + " canceled after " + FormatSeconds(trace.Trigger.WaitedSeconds) +
				" (" + FormatSeconds(trace.Trigger.NeededSeconds) + ")";
			break;
		default:
			text += " -> " + std::string(GetActionPhaseName(trace.Trigger.Phase)) + (trace.Trigger.bTriggered ? ", fired" : "");
			break;
		}
		return text;
	}
}

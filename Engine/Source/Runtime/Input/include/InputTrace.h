#pragma once

#include "InputActions.h"
#include "InputPipeline.h"
#include "InputTriggers.h"
#include "InputTypes.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// The router writes down every decision as it makes it: per binding, what its controls gave, what
// each pipeline step made of that, what the trigger did, and where and why it stopped. Debug
// windows and tests only read these records; nothing is worked out a second time.
namespace Nyx::Input
{
	class InputSystem;
	struct InputBinding;

	enum class EBindingOutcome : uint8_t
	{
		ContextOff,       // Its context isn't on the stack
		MissingModifiers, // Ctrl, Shift, Alt or Super isn't held
		Taken,            // A context above took its controls
		Idle,             // Its controls give nothing
		Zeroed,           // A pipeline step made the value 0
		Waiting,          // The trigger waits (Hold, Tap, DoubleTap)
		Canceled,         // The trigger gave up (held too short or too long)
		Contributed,      // It gave its action a value or fired it
	};

	const char* GetBindingOutcomeName(EBindingOutcome outcome);

	struct BindingTrace
	{
		ActionHandle Action;
		ContextHandle Context;
		uint32_t BindingIndex = 0;
		InputDeviceId Device;

		glm::vec3 Raw{ 0.0f };
		std::array<std::optional<glm::vec3>, PipelineStepCount> AfterStep;
		glm::vec3 Value{ 0.0f };

		TriggerResult Trigger;
		EBindingOutcome Outcome = EBindingOutcome::Idle;

		// For Taken: the context that took them and which of the binding's controls
		ContextHandle TakenBy;
		std::vector<ControlId> TakenControls;

		// For Zeroed: the step that made it 0
		EPipelineStep ZeroedBy = EPipelineStep::Count;
	};

	// The binding's controls as text: "Keyboard/W", "Ctrl+Alt+Shift+Keyboard/C",
	// "Keyboard/D,A,E,Q,W,S" for keys making up an axis
	std::string DescribeBindingSource(const InputBinding& binding);

	// Two lines, e.g.
	//   Jump   OnFoot (0) - Keyboard/Space - Pressed
	//   pressed -> stopped: taken by Menu (100)
	std::string FormatTrace(const InputSystem& input, const BindingTrace& trace);

	// "(0.08, 0.03, 0)", as traces print values
	std::string FormatValue(const glm::vec3& value);
}

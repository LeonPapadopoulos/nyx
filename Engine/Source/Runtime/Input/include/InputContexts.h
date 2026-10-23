#pragma once

#include "InputActions.h"
#include "InputPipeline.h"
#include "InputTriggers.h"
#include "InputTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Input
{
	// Keys that must be held for a binding, either the left or the right one
	enum class EModifierKeys : uint8_t
	{
		None = 0,
		Ctrl = 1 << 0,
		Shift = 1 << 1,
		Alt = 1 << 2,
		Super = 1 << 3,
	};

	constexpr EModifierKeys operator|(EModifierKeys a, EModifierKeys b)
	{
		return static_cast<EModifierKeys>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	constexpr bool HasModifier(EModifierKeys keys, EModifierKeys modifier)
	{
		return (static_cast<uint8_t>(keys) & static_cast<uint8_t>(modifier)) != 0;
	}

	// One axis to three from several controls: x is Right minus Left, y Up minus Down, z Forward
	// minus Back. Buttons add 1 while down, analog controls (e.g. triggers) their value; controls
	// left out add nothing.
	struct CompositeKeys
	{
		std::optional<ControlId> Right;
		std::optional<ControlId> Left;
		std::optional<ControlId> Up;
		std::optional<ControlId> Down;
		std::optional<ControlId> Forward;
		std::optional<ControlId> Back;

		// Scales the vector down to length 1 when it is longer, so W and D together aren't
		// faster than W
		bool bNormalize = true;
	};

	// One way to drive an action: a control (or keys making up an axis), what the value goes
	// through, and when it fires
	struct InputBinding
	{
		ActionHandle Action;

		// Either one control...
		std::optional<ControlId> Control;
		// ...whose axes go to x, y and z: the axis of the control for each (0 x, 1 y, 2 z), or
		// -1 for 0. E.g. { 1, -1, -1 }: only the wheel's y, as x; { 0, -1, 1 }: a stick's up as
		// forward (z).
		glm::ivec3 Swizzle{ 0, 1, 2 };

		// ...or keys making up an axis
		std::optional<CompositeKeys> Composite;

		// Keys that must be held too, e.g. Ctrl. The binding doesn't take them.
		EModifierKeys Modifiers = EModifierKeys::None;

		ValuePipeline Pipeline;
		TriggerSettings Trigger;

		// While its context is on, the binding takes its controls: contexts below don't get them.
		// Off: it lets them through.
		bool bTakesControls = true;

		static InputBinding ForControl(ActionHandle action, ControlId control, ETriggerKind trigger = ETriggerKind::Down)
		{
			InputBinding binding;
			binding.Action = action;
			binding.Control = control;
			binding.Trigger.Kind = trigger;
			return binding;
		}

		static InputBinding ForComposite(ActionHandle action, CompositeKeys keys)
		{
			InputBinding binding;
			binding.Action = action;
			binding.Composite = std::move(keys);
			return binding;
		}
	};

	// A bit per EDeviceKind, e.g. for the devices a context takes as a whole
	constexpr uint32_t DeviceBit(EDeviceKind device)
	{
		return 1u << static_cast<uint32_t>(device);
	}

	inline constexpr uint32_t AllDevices = 0xFFFFFFFFu;

	// A named set of bindings that is on or off as a whole, e.g. OnFoot, Menu, EditorFlyCamera.
	// Each user has a stack of them by priority: the highest context that binds a control gets it.
	struct InputContextDef
	{
		std::string Name;
		int Priority = 0;
		std::vector<InputBinding> Bindings;

		// What the cursor should be while the context is on. The topmost context that says
		// something decides; nothing sets the cursor otherwise.
		std::optional<ECursorMode> Cursor;

		// Devices it takes as a whole while on (DeviceBit()), e.g. everything while the program
		// has no focus
		uint32_t TakesDevices = 0;

		// The bindings as a pad model's profile tunes them (InputAsset.h): the same bindings, in
		// the same order and with the same controls, with other pipelines and triggers. A binding
		// a pad of that model drives uses its variant.
		struct ModelBindings
		{
			EGamepadModel Model = EGamepadModel::Unknown;
			std::vector<InputBinding> Bindings;
		};
		std::vector<ModelBindings> ModelVariants;
	};

	// Why a context is on, and what for
	struct ContextActivation
	{
		// Shown in traces and the debug window, e.g. "Scene hovered"
		std::string Reason;

		// For the context's cursor: the window it is about (null: the main window)
		void* Window = nullptr;

		// Overrides InputContextDef::TakesDevices while on
		std::optional<uint32_t> TakesDevices;
	};
}

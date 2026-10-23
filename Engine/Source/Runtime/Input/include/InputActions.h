#pragma once

#include "ReflectionMacros.h"
#include "InputTypes.h"
#include "TypedHandle.h"

#include <cstdint>
#include <string>

namespace Nyx::Input
{
	struct InputActionTag
	{
	};

	struct InputContextTag
	{
	};

	// An action or context the input system knows, from InputSystem::AddAction() / AddContext()
	using ActionHandle = TypedHandle<InputActionTag>;
	using ContextHandle = TypedHandle<InputContextTag>;

	NYX_ENUM()
	enum class EActionValue : uint8_t
	{
		Button, // Value.x: 1 while triggered, else 0
		Axis1D, // Value.x
		Axis2D, // Value.x, y
		Axis3D, // Value.x, y, z
	};

	// What the game or editor wants to do, e.g. Move or Jump, no matter with which key or stick
	struct InputActionDef
	{
		std::string Name;
		EActionValue ValueType = EActionValue::Button;
	};

	// Where an action or a binding's trigger is. A Hold of 0.5 s is Started at the press, Ongoing
	// while held, Triggered from 0.5 s on, Completed when let go after that, Canceled when let go
	// before.
	enum class EActionPhase : uint8_t
	{
		Idle,
		Started,
		Ongoing,
		Triggered,
		Completed,
		Canceled,
	};

	const char* GetActionPhaseName(EActionPhase phase);

	// An action in one frame, for one user
	struct ActionState
	{
		glm::vec3 Value{ 0.0f };
		EActionPhase Phase = EActionPhase::Idle;

		// It fired in this frame. Down fires every frame while held, Pressed once, Repeat now and
		// then; see ETriggerKind.
		bool bTriggered = false;

		// It left Idle (or Completed or Canceled) in this frame: the press that starts it
		bool bStarted = false;

		// How long the binding that decided it has been held, in seconds; 0 while it isn't
		float HeldSeconds = 0.0f;

		// The binding that decided it: its context, its place there, and the device
		ContextHandle Context;
		uint32_t BindingIndex = 0;
		InputDeviceId Device;

		float Float() const
		{
			return Value.x;
		}

		glm::vec2 Vec2() const
		{
			return glm::vec2(Value);
		}

		glm::vec3 Vec3() const
		{
			return Value;
		}

		bool IsTriggered() const
		{
			return Phase == EActionPhase::Triggered;
		}
	};
}

#include "Generated/Runtime/InputActions.reflect.h"

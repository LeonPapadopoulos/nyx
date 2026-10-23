#pragma once

#include "ReflectionMacros.h"
#include "InputActions.h"

#include <cstdint>
#include <string>

// When a binding fires its action. A trigger sees its control as down or up (an analog value
// counts as down from PressThreshold and up again below ReleaseThreshold), with the presses and
// releases of the frame and when they happened.
namespace Nyx::Input
{
	NYX_ENUM()
	enum class ETriggerKind : uint8_t
	{
		Down,      // Fires every frame while down; a tap within one frame fires once
		Pressed,   // Fires once, when pressed
		Released,  // Fires once, when let go
		Toggle,    // Each press turns it on or off; fires every frame while on
		Hold,      // Fires once held for HoldSeconds; every frame after that with bHoldRepeats
		Tap,       // Fires when let go within TapMaxSeconds of the press
		DoubleTap, // Fires on a second press within DoubleTapMaxGapSeconds of the first
		Chord,     // Like Down, but only while ChordAction is triggered
		Repeat,    // Fires when pressed, after RepeatDelaySeconds again, then every RepeatIntervalSeconds
	};

	const char* GetTriggerKindName(ETriggerKind kind);

	NYX_REFLECT()
	struct TriggerSettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		ETriggerKind Kind = ETriggerKind::Down;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float HoldSeconds = 0.5f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bHoldRepeats = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float TapMaxSeconds = 0.2f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float DoubleTapMaxGapSeconds = 0.3f;
		// For Chord: the other action, by name in assets (ChordActionName), by handle at run time
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string ChordActionName;
		ActionHandle ChordAction;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float RepeatDelaySeconds = 0.4f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float RepeatIntervalSeconds = 0.1f;

		// A firing that the code hasn't taken with InputUser::ConsumeTriggered() stays available
		// this long, e.g. a Jump pressed just before landing still jumps. 0: only in its frame.
		NYX_PROPERTY(Edit, Undo, Serialize)
		float BufferSeconds = 0.0f;

		// For analog values: down from PressThreshold, up again below ReleaseThreshold, so a
		// value resting near the threshold doesn't flicker
		NYX_PROPERTY(Edit, Undo, Serialize)
		float PressThreshold = 0.5f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float ReleaseThreshold = 0.4f;
	};

	// What the binding's control did in the frame
	struct TriggerInput
	{
		bool bDown = false;
		uint16_t Presses = 0;
		uint16_t Releases = 0;

		// When it was last pressed and let go, and when the frame began; microseconds
		uint64_t PressUs = 0;
		uint64_t ReleaseUs = 0;
		uint64_t NowUs = 0;

		// For Chord: whether the other action is triggered
		bool bChordTriggered = false;
	};

	// What a trigger remembers from frame to frame
	struct TriggerState
	{
		EActionPhase Phase = EActionPhase::Idle;
		uint64_t PressUs = 0;
		bool bToggledOn = false;
		bool bHoldFired = false;
		bool bTapCanceled = false;
		bool bWaitingForSecondTap = false;
		uint64_t NextRepeatUs = 0;
	};

	struct TriggerResult
	{
		EActionPhase Phase = EActionPhase::Idle;
		bool bTriggered = false;

		// For triggers that wait (Hold, Tap, DoubleTap): how long it has waited and how long it
		// waits at most or needs, in seconds
		float WaitedSeconds = 0.0f;
		float NeededSeconds = 0.0f;
	};

	TriggerResult UpdateTrigger(const TriggerSettings& settings, TriggerState& state, const TriggerInput& input);
}

#include "Generated/Runtime/InputTriggers.reflect.h"

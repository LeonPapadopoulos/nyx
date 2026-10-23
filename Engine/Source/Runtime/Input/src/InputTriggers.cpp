#include "InputTriggers.h"

#include <algorithm>

namespace Nyx::Input
{
	namespace
	{
		float SecondsBetween(uint64_t fromUs, uint64_t toUs)
		{
			return toUs > fromUs ? static_cast<float>(toUs - fromUs) / 1'000'000.0f : 0.0f;
		}

		uint64_t ToUs(float seconds)
		{
			return seconds > 0.0f ? static_cast<uint64_t>(seconds * 1'000'000.0f) : 0;
		}

		bool IsActive(EActionPhase phase)
		{
			return phase == EActionPhase::Started || phase == EActionPhase::Ongoing || phase == EActionPhase::Triggered;
		}

		// Down and Chord: fired while held, Completed the frame after
		TriggerResult WhileHeld(bool bFires, const TriggerState& state)
		{
			TriggerResult result;
			if (bFires)
			{
				result.Phase = EActionPhase::Triggered;
				result.bTriggered = true;
			}
			else
			{
				result.Phase = state.Phase == EActionPhase::Triggered ? EActionPhase::Completed : EActionPhase::Idle;
			}
			return result;
		}
	}

	const char* GetActionPhaseName(EActionPhase phase)
	{
		switch (phase)
		{
		case EActionPhase::Idle:      return "Idle";
		case EActionPhase::Started:   return "Started";
		case EActionPhase::Ongoing:   return "Ongoing";
		case EActionPhase::Triggered: return "Triggered";
		case EActionPhase::Completed: return "Completed";
		case EActionPhase::Canceled:  return "Canceled";
		}
		return "Unknown";
	}

	const char* GetTriggerKindName(ETriggerKind kind)
	{
		switch (kind)
		{
		case ETriggerKind::Down:      return "Down";
		case ETriggerKind::Pressed:   return "Pressed";
		case ETriggerKind::Released:  return "Released";
		case ETriggerKind::Toggle:    return "Toggle";
		case ETriggerKind::Hold:      return "Hold";
		case ETriggerKind::Tap:       return "Tap";
		case ETriggerKind::DoubleTap: return "DoubleTap";
		case ETriggerKind::Chord:     return "Chord";
		case ETriggerKind::Repeat:    return "Repeat";
		}
		return "Unknown";
	}

	TriggerResult UpdateTrigger(const TriggerSettings& settings, TriggerState& state, const TriggerInput& input)
	{
		const bool bPressed = input.Presses > 0;
		const bool bReleased = input.Releases > 0;
		TriggerResult result;

		switch (settings.Kind)
		{
		case ETriggerKind::Down:
			result = WhileHeld(input.bDown || bPressed, state);
			break;

		case ETriggerKind::Chord:
			result = WhileHeld((input.bDown || bPressed) && input.bChordTriggered, state);
			break;

		case ETriggerKind::Pressed:
			result.Phase = bPressed ? EActionPhase::Triggered : EActionPhase::Idle;
			result.bTriggered = bPressed;
			break;

		case ETriggerKind::Released:
			if (bReleased)
			{
				result.Phase = EActionPhase::Triggered;
				result.bTriggered = true;
			}
			else if (input.bDown)
			{
				result.Phase = bPressed ? EActionPhase::Started : EActionPhase::Ongoing;
			}
			break;

		case ETriggerKind::Toggle:
		{
			const bool bWasOn = state.bToggledOn;
			if (input.Presses % 2 == 1)
			{
				state.bToggledOn = !state.bToggledOn;
			}
			if (state.bToggledOn)
			{
				result.Phase = EActionPhase::Triggered;
				result.bTriggered = true;
			}
			else
			{
				result.Phase = bWasOn ? EActionPhase::Completed : EActionPhase::Idle;
			}
			break;
		}

		case ETriggerKind::Hold:
		{
			result.NeededSeconds = settings.HoldSeconds;
			if (input.bDown && (bPressed || !IsActive(state.Phase)))
			{
				// A control already held when the binding comes on (its context was off) counts
				// from now
				state.PressUs = bPressed ? input.PressUs : input.NowUs;
				state.bHoldFired = false;
			}

			if (input.bDown)
			{
				result.WaitedSeconds = SecondsBetween(state.PressUs, input.NowUs);
				if (input.NowUs >= state.PressUs + ToUs(settings.HoldSeconds))
				{
					result.Phase = EActionPhase::Triggered;
					result.bTriggered = !state.bHoldFired || settings.bHoldRepeats;
					state.bHoldFired = true;
				}
				else
				{
					result.Phase = bPressed ? EActionPhase::Started : EActionPhase::Ongoing;
				}
			}
			else if (bPressed || IsActive(state.Phase))
			{
				// Let go: after it fired, it completed; before, or within one frame, it didn't make it
				result.Phase = state.bHoldFired && !bPressed ? EActionPhase::Completed : EActionPhase::Canceled;
				state.bHoldFired = false;
			}
			break;
		}

		case ETriggerKind::Tap:
		{
			result.NeededSeconds = settings.TapMaxSeconds;
			if (bPressed)
			{
				state.PressUs = input.PressUs;
				state.bTapCanceled = false;
			}
			else if (input.bDown && !IsActive(state.Phase) && state.Phase != EActionPhase::Canceled)
			{
				// Already held when the binding came on: not a tap
				state.bTapCanceled = true;
			}

			const bool bWasHeld = bPressed || IsActive(state.Phase);
			if (bReleased && bWasHeld && !input.bDown)
			{
				result.WaitedSeconds = SecondsBetween(state.PressUs, input.ReleaseUs);
				if (!state.bTapCanceled && input.ReleaseUs <= state.PressUs + ToUs(settings.TapMaxSeconds))
				{
					result.Phase = EActionPhase::Triggered;
					result.bTriggered = true;
				}
			}
			else if (input.bDown && !state.bTapCanceled)
			{
				result.WaitedSeconds = SecondsBetween(state.PressUs, input.NowUs);
				if (input.NowUs > state.PressUs + ToUs(settings.TapMaxSeconds))
				{
					// Held too long for a tap
					result.Phase = EActionPhase::Canceled;
					state.bTapCanceled = true;
				}
				else
				{
					result.Phase = bPressed ? EActionPhase::Started : EActionPhase::Ongoing;
				}
			}
			break;
		}

		case ETriggerKind::DoubleTap:
		{
			result.NeededSeconds = settings.DoubleTapMaxGapSeconds;
			const uint64_t maxGapUs = ToUs(settings.DoubleTapMaxGapSeconds);
			if (input.Presses >= 2 || (bPressed && state.bWaitingForSecondTap && input.PressUs <= state.PressUs + maxGapUs))
			{
				result.WaitedSeconds = input.Presses >= 2 ? 0.0f : SecondsBetween(state.PressUs, input.PressUs);
				result.Phase = EActionPhase::Triggered;
				result.bTriggered = true;
				state.bWaitingForSecondTap = false;
			}
			else if (bPressed)
			{
				state.PressUs = input.PressUs;
				state.bWaitingForSecondTap = true;
				result.Phase = EActionPhase::Started;
			}
			else if (state.bWaitingForSecondTap)
			{
				result.WaitedSeconds = SecondsBetween(state.PressUs, input.NowUs);
				if (input.NowUs > state.PressUs + maxGapUs)
				{
					result.Phase = EActionPhase::Canceled;
					state.bWaitingForSecondTap = false;
				}
				else
				{
					result.Phase = EActionPhase::Ongoing;
				}
			}
			break;
		}

		case ETriggerKind::Repeat:
		{
			if (bPressed)
			{
				result.Phase = EActionPhase::Triggered;
				result.bTriggered = true;
				state.NextRepeatUs = input.PressUs + ToUs(settings.RepeatDelaySeconds);
			}
			else if (input.bDown)
			{
				result.Phase = EActionPhase::Triggered;
				if (input.NowUs >= state.NextRepeatUs)
				{
					result.bTriggered = true;
					// Once per frame at most; a long frame doesn't fire several times
					const uint64_t intervalUs = std::max<uint64_t>(ToUs(settings.RepeatIntervalSeconds), 1);
					while (state.NextRepeatUs <= input.NowUs)
					{
						state.NextRepeatUs += intervalUs;
					}
				}
			}
			else
			{
				result.Phase = state.Phase == EActionPhase::Triggered ? EActionPhase::Completed : EActionPhase::Idle;
			}
			break;
		}
		}

		state.Phase = result.Phase;
		return result;
	}
}

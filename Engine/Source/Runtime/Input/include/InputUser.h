#pragma once

#include "InputActions.h"
#include "InputContexts.h"
#include "InputFrame.h"
#include "InputOutput.h"
#include "InputTrace.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Nyx::Input
{
	class InputSystem;

	// A player: their stack of contexts, their actions' states, and the device they used last.
	// There is one from the start, owning every device.
	//
	// The stack is the only thing that decides who gets a control. Each frame the user goes down
	// it from the highest priority; the first context that binds a control gets it and, unless the
	// binding lets it through, takes it from the contexts below. Focus, ImGui and the like are
	// contexts on the stack too (InputSystem's system contexts), so every "why not" has the same
	// kind of answer: taken by a context, at a priority.
	class InputUser
	{
	public:
		struct StackEntry
		{
			ContextHandle Context;
			ContextActivation Activation;
		};

		const ActionState& Get(ActionHandle action) const;

		// True once for each firing: the code that acts on it takes it. A firing nobody takes is
		// gone at the next frame, or after its trigger's BufferSeconds.
		bool ConsumeTriggered(ActionHandle action);

		// Puts the context on the stack or takes it off. Turning on a context that is on updates
		// its activation. Changes count from the next frame on: the stack is read once per frame.
		void SetContextActive(ContextHandle context, bool bActive, ContextActivation activation = {});
		bool IsContextActive(ContextHandle context) const;

		// The contexts that are on, the highest first (by priority; of equal ones, the later on)
		std::vector<StackEntry> GetStack() const;

		// Every binding of every context, as the last frame decided it
		const std::vector<BindingTrace>& GetTraces() const
		{
			return Traces;
		}

		// The traces of one action's bindings: what "why didn't it fire" needs
		std::vector<const BindingTrace*> GetTraces(ActionHandle action) const;

		// What the stack made of the cursor at the end of the last frame: the topmost context that
		// says something about it (InputContextDef::Cursor), or free. Owner is that context's name.
		const CursorRequest& GetCursor() const
		{
			return Cursor;
		}

		// Output for the user's gamepads, asked during the frame and sent at its end. Several asks
		// in one frame are merged: the stronger rumble, the later effect or colour.
		void Rumble(const RumbleOutput& rumble);
		void TriggerRumble(const TriggerRumbleOutput& rumble);
		void SetTriggerEffect(ETrigger trigger, const TriggerEffect& effect);
		void SetLightColor(const glm::vec3& color);
		void SetPlayerIndex(int playerIndex);

		// Scales rumble and trigger rumble, 0 to 1; 0 turns them off
		void SetHapticsStrength(float strength);
		float GetHapticsStrength() const
		{
			return HapticsStrength;
		}

		// What the last frame sent to which device, and what it couldn't send and why, e.g. that
		// an Xbox pad has no adaptive triggers
		struct OutputRecord
		{
			InputDeviceId Device;
			std::string What;
			bool bSent = false;
			std::string Reason;
		};

		const std::vector<OutputRecord>& GetOutputRecords() const
		{
			return OutputRecords;
		}

		// For prompts: the device last used on purpose. A stick or mouse that barely moves doesn't
		// count, so the prompts don't flip back and forth.
		InputDeviceId GetLastUsedDevice() const
		{
			return LastUsedDevice;
		}

	private:
		friend class InputSystem;

		struct BindingRuntime
		{
			TriggerState Trigger;
			PipelineState Pipeline;
			// For analog values: down by the press/release thresholds
			bool bActuated = false;
			uint64_t PressUs = 0;
		};

		struct ActiveContext
		{
			ContextActivation Activation;
			uint64_t Order = 0;
		};

		void Evaluate(const InputSystem& input, const InputFrame& frame, float deltaSeconds);

		// Reads the cursor off the stack as it is now
		void DecideCursor();

		// Highest priority first; of equal ones, the one turned on later
		void SortByStackOrder(std::vector<uint32_t>& contextIndices) const;
		void UpdateLastUsedDevice(const InputFrame& frame);

		// The input system the user belongs to, for its actions and contexts
		const InputSystem* System = nullptr;

		std::vector<ActionState> Actions;
		std::vector<ActionState> PreviousActions;

		// The firing each action keeps for ConsumeTriggered(), and until when
		struct PendingTrigger
		{
			bool bPending = false;
			uint64_t ExpiresUs = 0;
		};
		std::vector<PendingTrigger> Pending;

		// By context index
		std::vector<std::optional<ActiveContext>> Active;
		std::vector<std::vector<BindingRuntime>> Bindings;
		uint64_t ActivationCount = 0;

		std::vector<BindingTrace> Traces;
		CursorRequest Cursor;

		DeviceOutput PendingOutput;
		float HapticsStrength = 1.0f;
		std::vector<OutputRecord> OutputRecords;
		InputDeviceId LastUsedDevice;
	};
}

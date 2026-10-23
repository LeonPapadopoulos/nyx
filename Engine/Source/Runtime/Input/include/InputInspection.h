#pragma once

#include "InputSystem.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Questions the debug window asks of the input system's data, answered the way the router
// decides: who gets a control with a given stack, which bindings use a control, what a stick's
// dead zones should be.
namespace Nyx::Input
{
	inline constexpr uint32_t NoBinding = UINT32_MAX;

	// One binding that sees a control, or a context that takes its whole device (BindingIndex is
	// NoBinding then)
	struct ControlUse
	{
		ContextHandle Context;
		uint32_t BindingIndex = NoBinding;
		ActionHandle Action;

		// It takes the control: the contexts below don't see it. A binding with modifier keys
		// takes it only while they are held, so it is false here.
		bool bTakes = false;
	};

	// The controls a binding reads: its control, or its composite's keys. Modifier keys aren't.
	std::vector<ControlId> GetBindingControls(const InputBinding& binding);

	// Who gets the control with this stack (highest first, as InputUser::GetStack() lists it):
	// every binding that sees it, from the top down to the context that takes it. Empty: nothing
	// binds it.
	std::vector<ControlUse> ResolveControl(const InputSystem& input, std::span<const InputUser::StackEntry> stack, ControlId control);

	// Every binding of every context that reads the control, whether its context is on or not
	std::vector<ControlUse> FindBindingsOf(const InputSystem& input, ControlId control);

	// A stack made of the given contexts, as it would be if they were on: by priority, the
	// highest first; the reason says it is made up
	std::vector<InputUser::StackEntry> MakeStack(const InputSystem& input, std::span<const ContextHandle> contexts);

	// Measures a stick by hand and suggests its dead zones:
	// - drift: the stick is left alone; the inner dead zone should hold all the places it rests at,
	//   with a margin;
	// - range: the stick is turned around its edge; the outer dead zone should be reached all the
	//   way round, so full deflection gives 1 in every direction.
	class StickCalibration
	{
	public:
		enum class EMeasure : uint8_t
		{
			Drift,
			Range,
		};

		struct Result
		{
			EMeasure Measure = EMeasure::Drift;
			// The suggested dead zone; unset when the measurement wasn't good enough
			std::optional<float> Suggested;
			// What the suggestion is based on, or what was missing
			std::string Explanation;
		};

		// The stick of the device is sampled every frame (Sample()) for `seconds`
		void Start(EMeasure measure, InputDeviceId device, ControlId stick, uint64_t nowUs, float seconds);
		void Cancel();
		void Sample(const InputFrame& frame);

		bool IsRunning() const
		{
			return bRunning;
		}

		// 0 to 1 while running
		float GetProgress(uint64_t nowUs) const;

		InputDeviceId GetDevice() const
		{
			return Device;
		}

		ControlId GetStick() const
		{
			return Stick;
		}

		const std::vector<glm::vec2>& GetSamples() const
		{
			return Samples;
		}

		// The last finished measurement
		const std::optional<Result>& GetResult() const
		{
			return LastResult;
		}

		// The longest distance from the middle, times 1.2 and 0.02 more, rounded up to 0.01; at
		// least 0.05
		static Result SuggestInnerDeadZone(std::span<const glm::vec2> samples);

		// The edge in 32 directions: the shortest of the farthest reaches, less 0.01, rounded down
		// to 0.01. Needs 28 of the 32 directions reached.
		static Result SuggestOuterDeadZone(std::span<const glm::vec2> samples);

		static constexpr int RangeSectors = 32;
		static constexpr int RangeSectorsNeeded = 28;

	private:
		void Finish();

		bool bRunning = false;
		EMeasure Measure = EMeasure::Drift;
		InputDeviceId Device;
		ControlId Stick;
		uint64_t StartUs = 0;
		uint64_t EndUs = 0;
		std::vector<glm::vec2> Samples;
		std::optional<Result> LastResult;
	};
}

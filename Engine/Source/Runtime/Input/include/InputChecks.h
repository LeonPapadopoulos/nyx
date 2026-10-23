#pragma once

#include "InputAsset.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Checks on an input asset: mistakes that are easy to make and hard to feel, each with a name, the
// binding it is about and, where the fix is obvious, the fix. The editor shows them in the Input
// window's Checks tab and beside the binding; loading an asset logs them.
namespace Nyx::Input
{
	enum class EInputCheck : uint8_t
	{
		// Two actions on the same control in the same context, without a tap/hold split
		Conflict,
		// A binding of a context that is on, whose control a higher context that is on takes
		Unreachable,
		// An action without a binding for keyboard and mouse, or for pads
		Coverage,
		// An action the code takes once (ConsumeTriggered) bound with Down: fires every frame held
		DownOnOneShot,
		// Mouse/Delta times the frame's time: aiming then depends on the frame rate
		DeltaTimeOnMouse,
		// A stick turning a camera without the frame's time: turning then depends on the frame rate
		NoDeltaTimeOnStickRate,
		// Keys making up an axis without normalizing: two at once are faster than one
		CompositeNotNormalized,
		// Inner at or past outer, or ignoring more than half the travel
		DeadZone,
		// A hold on the same control as a tap, no longer than the tap
		TapHoldOverlap,
		// Smoothing above 30 ms: that much latency
		SmoothingLatency,
		// An analog control used as a button that lets go where it presses: it flickers
		ThresholdWithoutHysteresis,
		// A control some pad models don't have, without another binding for them
		DeviceOnlyControl,
		Count
	};

	const char* GetInputCheckName(EInputCheck check);

	// Sets one value of one binding, as an override would (ApplyOverride)
	struct InputCheckFix
	{
		// E.g. "Turn on Normalize"
		std::string Label;
		size_t Context = 0;
		size_t Binding = 0;
		std::string Field;
		float Value = 0.0f;
	};

	struct InputCheckFinding
	{
		EInputCheck Check = EInputCheck::Conflict;

		// The binding it is about; for Coverage, none: the action
		std::optional<size_t> Context;
		std::optional<size_t> Binding;
		std::string Action;

		std::string Message;
		std::optional<InputCheckFix> Fix;
	};

	// What only the running program knows
	struct InputCheckRuntime
	{
		// The contexts that are on, by name (Unreachable)
		std::vector<std::string> ActiveContexts;
		// The actions the code called ConsumeTriggered on (DownOnOneShot)
		std::vector<std::string> ConsumedActions;
	};

	// The contexts on the input system's stack and the actions its code consumed
	InputCheckRuntime MakeCheckRuntime(const InputSystem& input);

	std::vector<InputCheckFinding> RunInputChecks(const InputAssetData& asset, const InputCheckRuntime* runtime = nullptr);

	// The findings about one binding
	std::vector<const InputCheckFinding*> FindingsFor(const std::vector<InputCheckFinding>& findings, size_t context, size_t binding);

	// False if the fix's binding or field isn't there
	bool ApplyFix(InputAssetData& asset, const InputCheckFix& fix);

	// E.g. "EditorFlyCamera, binding 3 (Look: Gamepad/RightStick)", or "action Jump"
	std::string DescribeWhere(const InputAssetData& asset, const InputCheckFinding& finding);

	// "Gamepad/LeftStick", or the composite's controls: "Keyboard/D, Keyboard/A"
	std::string DescribeControls(const InputBindingData& binding);

	// Whether every pad of the model has the control: e.g. only PlayStation pads have a touchpad.
	// Unknown and Generic pads have the controls every pad has.
	bool ModelHasControl(EGamepadModel model, EGamepadControl control);

	// Which devices can do each action
	enum class ECoverageColumn : uint8_t
	{
		KeyboardMouse,
		Xbox,
		DualShock4,
		DualSense,
		Count
	};

	const char* GetCoverageColumnName(ECoverageColumn column);

	struct CoverageRow
	{
		std::string Action;
		EActionDevices Devices = EActionDevices::All;
		std::array<bool, static_cast<size_t>(ECoverageColumn::Count)> bCovered{};

		// Whether a column the action should cover isn't
		bool HasGap() const;
		bool IsExpected(ECoverageColumn column) const;
	};

	std::vector<CoverageRow> ComputeCoverage(const InputAssetData& asset);
}

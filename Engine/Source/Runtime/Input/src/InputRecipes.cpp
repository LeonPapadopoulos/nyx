#include "InputRecipes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Nyx::Input
{
	namespace
	{
		constexpr float RadiansToDegrees = 57.29578f;

		InputBindingData OnControl(const std::string& action, const std::string& control, ETriggerKind kind = ETriggerKind::Down)
		{
			InputBindingData binding;
			binding.Action = action;
			binding.Control = control;
			binding.Trigger.Kind = kind;
			return binding;
		}

		InputBindingData OnComposite(const std::string& action, CompositeKeysData keys, ETriggerKind kind = ETriggerKind::Down)
		{
			InputBindingData binding;
			binding.Action = action;
			binding.Composite = std::move(keys);
			binding.Trigger.Kind = kind;
			return binding;
		}

		std::string Format(const char* format, double value)
		{
			char text[64];
			std::snprintf(text, sizeof(text), format, value);
			return text;
		}

		std::string Milliseconds(float seconds)
		{
			return std::to_string(static_cast<int>(std::lround(seconds * 1000.0f))) + " ms";
		}

		// "18 frames at 60 fps"
		std::string Frames(float seconds)
		{
			return std::to_string(static_cast<int>(std::lround(seconds * 60.0f))) + " frames at 60 fps";
		}

		std::string Percent(float fraction)
		{
			return Format("%.0f %%", fraction * 100.0);
		}
	}

	const char* GetRecipeName(EInputRecipe recipe)
	{
		switch (recipe)
		{
		case EInputRecipe::Press:      return "Do it once per press";
		case EInputRecipe::Hold:       return "Do it while held";
		case EInputRecipe::Charge:     return "Charge, release to do it";
		case EInputRecipe::TapAndHold: return "Tap for one thing, hold for another";
		case EInputRecipe::Move:       return "Move";
		case EInputRecipe::Look:       return "Look or aim";
		case EInputRecipe::Amount:     return "An analog amount";
		case EInputRecipe::MenuStep:   return "Step through a menu";
		case EInputRecipe::Count:      break;
		}
		return "Unknown";
	}

	const char* GetRecipeDescription(EInputRecipe recipe)
	{
		switch (recipe)
		{
		case EInputRecipe::Press:
			return "Jump, interact. Space and the bottom face button, Pressed; a press is kept 100 ms for code that asks a little later.";
		case EInputRecipe::Hold:
			return "Sprint, block. Left Shift and the left stick's button, Down. Players who'd rather toggle set its trigger to Toggle in their settings.";
		case EInputRecipe::Charge:
			return "Charged shot. The left mouse button and the right trigger, Released; the code reads how long it was held.";
		case EInputRecipe::TapAndHold:
			return "Tap: interact, hold: reload. Two actions on E and the left face button: the name with Tap (200 ms), and name + \"Hold\" "
				   "with Hold (300 ms), longer than the tap.";
		case EInputRecipe::Move:
			return "2D: W/A/S/D, normalized so diagonals aren't faster, and the left stick with a scaled radial dead zone.";
		case EInputRecipe::Look:
			return "2D, in radians to turn: the right stick as a rate (power curve, look acceleration, times the frame's time) and the "
				   "mouse's movement as a distance (sensitivity only).";
		case EInputRecipe::Amount:
			return "Throttle, brake. 1D: the right trigger with a small dead zone, and W for full.";
		case EInputRecipe::MenuStep:
			return "2D steps: the D-pad, the arrow keys and the left stick, each Repeat (400 ms, then every 100 ms); the stick lets go well "
				   "below where it presses.";
		case EInputRecipe::Count:
			break;
		}
		return "";
	}

	std::vector<std::string> AddRecipe(InputAssetData& asset, size_t contextIndex, EInputRecipe recipe, const std::string& actionName)
	{
		const auto exists = [&asset](const std::string& name)
		{
			return std::any_of(asset.Actions.begin(), asset.Actions.end(),
				[&name](const InputActionData& action)
				{
					return action.Name == name;
				});
		};
		const std::string holdName = actionName + "Hold";
		if (contextIndex >= asset.Contexts.size() || actionName.empty() || exists(actionName) ||
			(recipe == EInputRecipe::TapAndHold && exists(holdName)))
		{
			return {};
		}

		std::vector<InputBindingData>& bindings = asset.Contexts[contextIndex].Bindings;
		const std::string& name = actionName;
		switch (recipe)
		{
		case EInputRecipe::Press:
		{
			asset.Actions.push_back({ name, EActionValue::Button });
			for (const char* control : { "Keyboard/Space", "Gamepad/FaceSouth" })
			{
				InputBindingData binding = OnControl(name, control, ETriggerKind::Pressed);
				binding.Trigger.BufferSeconds = 0.1f;
				bindings.push_back(binding);
			}
			return { name };
		}

		case EInputRecipe::Hold:
			asset.Actions.push_back({ name, EActionValue::Button });
			bindings.push_back(OnControl(name, "Keyboard/LeftShift"));
			bindings.push_back(OnControl(name, "Gamepad/LeftStickButton"));
			return { name };

		case EInputRecipe::Charge:
			asset.Actions.push_back({ name, EActionValue::Button });
			bindings.push_back(OnControl(name, "Mouse/Left", ETriggerKind::Released));
			bindings.push_back(OnControl(name, "Gamepad/RightTrigger", ETriggerKind::Released));
			return { name };

		case EInputRecipe::TapAndHold:
		{
			asset.Actions.push_back({ name, EActionValue::Button });
			asset.Actions.push_back({ holdName, EActionValue::Button });
			for (const char* control : { "Keyboard/E", "Gamepad/FaceWest" })
			{
				InputBindingData tap = OnControl(name, control, ETriggerKind::Tap);
				tap.Trigger.TapMaxSeconds = 0.2f;
				bindings.push_back(tap);

				InputBindingData hold = OnControl(holdName, control, ETriggerKind::Hold);
				hold.Trigger.HoldSeconds = 0.3f;
				bindings.push_back(hold);
			}
			return { name, holdName };
		}

		case EInputRecipe::Move:
		{
			asset.Actions.push_back({ name, EActionValue::Axis2D });
			bindings.push_back(OnComposite(name, { .Right = "Keyboard/D", .Left = "Keyboard/A", .Up = "Keyboard/W", .Down = "Keyboard/S" }));
			InputBindingData stick = OnControl(name, "Gamepad/LeftStick");
			stick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.15f };
			bindings.push_back(stick);
			return { name };
		}

		case EInputRecipe::Look:
		{
			asset.Actions.push_back({ name, EActionValue::Axis2D });
			InputBindingData stick = OnControl(name, "Gamepad/RightStick");
			stick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.15f };
			stick.Pipeline.Curve = { .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
			stick.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(2.5f) };
			stick.Pipeline.LookAcceleration = { .bEnabled = true };
			stick.Pipeline.bMultiplyByDeltaTime = true;
			bindings.push_back(stick);

			// The mouse's y points down, the action's up
			InputBindingData mouse = OnControl(name, "Mouse/Delta");
			mouse.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(0.003f), .bInvertY = true };
			bindings.push_back(mouse);
			return { name };
		}

		case EInputRecipe::Amount:
		{
			asset.Actions.push_back({ name, EActionValue::Axis1D });
			InputBindingData trigger = OnControl(name, "Gamepad/RightTrigger");
			trigger.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.05f };
			bindings.push_back(trigger);
			bindings.push_back(OnControl(name, "Keyboard/W"));
			return { name };
		}

		case EInputRecipe::MenuStep:
		{
			asset.Actions.push_back({ name, EActionValue::Axis2D });
			const auto repeat = [](InputBindingData binding)
			{
				binding.Trigger.RepeatDelaySeconds = 0.4f;
				binding.Trigger.RepeatIntervalSeconds = 0.1f;
				return binding;
			};
			bindings.push_back(repeat(OnComposite(name,
				{ .Right = "Gamepad/DpadRight", .Left = "Gamepad/DpadLeft", .Up = "Gamepad/DpadUp", .Down = "Gamepad/DpadDown" },
				ETriggerKind::Repeat)));
			bindings.push_back(repeat(
				OnComposite(name, { .Right = "Keyboard/Right", .Left = "Keyboard/Left", .Up = "Keyboard/Up", .Down = "Keyboard/Down" },
					ETriggerKind::Repeat)));

			InputBindingData stick = repeat(OnControl(name, "Gamepad/LeftStick", ETriggerKind::Repeat));
			stick.Trigger.PressThreshold = 0.5f;
			stick.Trigger.ReleaseThreshold = 0.3f;
			bindings.push_back(stick);
			return { name };
		}

		case EInputRecipe::Count:
			break;
		}
		return {};
	}

	std::vector<TuningExplanation> ExplainBinding(const InputBindingData& binding, const InputActionData* action)
	{
		std::vector<TuningExplanation> lines;
		const auto add = [&lines](const char* field, std::string text)
		{
			lines.push_back({ field, std::move(text) });
		};

		const std::optional<ControlId> control = binding.Control.empty() ? std::nullopt : ParseControlPath(binding.Control);
		const bool bMouseDelta = control == Mouse(EMouseControl::Delta);
		const bool bStick = control == Gamepad(EGamepadControl::LeftStick) || control == Gamepad(EGamepadControl::RightStick);
		const bool bTrigger = control == Gamepad(EGamepadControl::LeftTrigger) || control == Gamepad(EGamepadControl::RightTrigger);
		const auto isAnalog = [](const std::string& path)
		{
			const std::optional<ControlId> part = ParseControlPath(path);
			return part && !IsButton(*part);
		};
		const CompositeKeysData& keys = binding.Composite;
		const bool bAnalog = control ? !IsButton(*control)
									 : isAnalog(keys.Right) || isAnalog(keys.Left) || isAnalog(keys.Up) || isAnalog(keys.Down) ||
				isAnalog(keys.Forward) || isAnalog(keys.Back);
		const char* travel = bStick ? "the stick's travel" : bTrigger ? "the trigger's travel"
																	  : "the control's travel";
		const bool b2D = action && action->ValueType == EActionValue::Axis2D;

		const ValuePipeline& pipeline = binding.Pipeline;
		if (pipeline.DeadZone.bEnabled)
		{
			add("Pipeline.DeadZone.Inner", "Inner " + Format("%g", pipeline.DeadZone.Inner) + ": ignores the first " + Percent(pipeline.DeadZone.Inner) + " of " + travel);
			add("Pipeline.DeadZone.Outer", "Outer " + Format("%g", pipeline.DeadZone.Outer) + ": full value from " + Percent(pipeline.DeadZone.Outer) + " of " + travel + " on");
		}

		if (pipeline.Curve.bEnabled && pipeline.Curve.Kind == ECurveKind::Power)
		{
			add("Pipeline.Curve.Exponent", "Power " + Format("%g", pipeline.Curve.Exponent) + ": half way gives " + Percent(std::pow(0.5f, pipeline.Curve.Exponent)) + (pipeline.Curve.Exponent > 1.0f ? ", finer near the middle" : pipeline.Curve.Exponent < 1.0f ? ", quicker off the middle"
																																																																	: ""));
		}
		else if (pipeline.Curve.bEnabled && pipeline.Curve.Kind == ECurveKind::Custom)
		{
			add("Pipeline.Curve.Points", "A custom curve through " + std::to_string(pipeline.Curve.Points.size()) + " points");
		}

		if (pipeline.Sensitivity.bEnabled)
		{
			const float scale = pipeline.Sensitivity.Scale.x;
			std::string text = "x " + Format("%g", scale);
			if (bMouseDelta && b2D)
			{
				text += ": moving the mouse 1000 pixels turns " + Format("%.0f degrees", scale * 1000.0f * RadiansToDegrees) + " (in radians)";
			}
			else if (bStick && b2D && pipeline.bMultiplyByDeltaTime)
			{
				text += " a second: full stick turns " + Format("%.0f degrees", scale * RadiansToDegrees) + " a second (in radians)";
			}
			add("Pipeline.Sensitivity.Scale", text);
			if (pipeline.Sensitivity.bInvertY)
			{
				add("Pipeline.Sensitivity.bInvertY", bMouseDelta ? "Inverts y: the mouse's y points down, so moving it up gives up" : "Inverts y: up gives down");
			}
		}

		const LookAccelerationSettings& look = pipeline.LookAcceleration;
		if (look.bEnabled)
		{
			add("Pipeline.LookAcceleration.MaxMultiplier", "Held past " + Percent(look.Threshold) + " for " + Milliseconds(look.DelaySeconds) + ", it speeds up to x " + Format("%g", look.MaxMultiplier) + " over " + Milliseconds(look.RampSeconds));
		}

		if (pipeline.Smoothing.bEnabled)
		{
			add("Pipeline.Smoothing.TimeConstantSeconds",
				"Follows the value about " + Milliseconds(pipeline.Smoothing.TimeConstantSeconds) + " behind: that much latency");
		}

		if (pipeline.bMultiplyByDeltaTime)
		{
			add("Pipeline.bMultiplyByDeltaTime", "Times the frame's time: the value is a rate per second");
		}

		const TriggerSettings& trigger = binding.Trigger;
		switch (trigger.Kind)
		{
		case ETriggerKind::Down:     add("Trigger.Kind", "Fires every frame while down"); break;
		case ETriggerKind::Pressed:  add("Trigger.Kind", "Fires once per press"); break;
		case ETriggerKind::Released: add("Trigger.Kind", "Fires once, when let go"); break;
		case ETriggerKind::Toggle:   add("Trigger.Kind", "Each press turns it on or off"); break;
		case ETriggerKind::Hold:
			add("Trigger.HoldSeconds", "Fires after " + Milliseconds(trigger.HoldSeconds) + " held: " + Frames(trigger.HoldSeconds) + (trigger.bHoldRepeats ? ", then every frame" : ""));
			break;
		case ETriggerKind::Tap:
			add("Trigger.TapMaxSeconds", "A tap if let go within " + Milliseconds(trigger.TapMaxSeconds) + ": " + Frames(trigger.TapMaxSeconds));
			break;
		case ETriggerKind::DoubleTap:
			add("Trigger.DoubleTapMaxGapSeconds", "Fires on a second press within " + Milliseconds(trigger.DoubleTapMaxGapSeconds) + ": " + Frames(trigger.DoubleTapMaxGapSeconds));
			break;
		case ETriggerKind::Chord:
			add("Trigger.ChordActionName", "Like Down, only while " + (trigger.ChordActionName.empty() ? std::string("<no action>") : trigger.ChordActionName) + " is triggered");
			break;
		case ETriggerKind::Repeat:
			add("Trigger.RepeatIntervalSeconds", "Fires on the press, again after " + Milliseconds(trigger.RepeatDelaySeconds) + ", then every " + Milliseconds(trigger.RepeatIntervalSeconds) + " (" + Format("%.0f", trigger.RepeatIntervalSeconds > 0.0f ? 1.0f / trigger.RepeatIntervalSeconds : 0.0f) + " a second)");
			break;
		}

		if (trigger.BufferSeconds > 0.0f)
		{
			add("Trigger.BufferSeconds", "A firing is kept " + Milliseconds(trigger.BufferSeconds) + " for code that asks a little later: " + Frames(trigger.BufferSeconds));
		}

		const bool bUsedAsButton = (action && action->ValueType == EActionValue::Button) || trigger.Kind != ETriggerKind::Down;
		if (bAnalog && bUsedAsButton && !bMouseDelta)
		{
			add("Trigger.PressThreshold", "Counts as pressed from " + Percent(trigger.PressThreshold) + ", let go below " + Percent(trigger.ReleaseThreshold));
		}
		return lines;
	}

	float SampleResponse(const ValuePipeline& pipeline, float in)
	{
		glm::vec3 value(in, 0.0f, 0.0f);
		if (pipeline.DeadZone.bEnabled)
		{
			value = ApplyDeadZone(value, pipeline.DeadZone);
		}
		if (pipeline.Curve.bEnabled)
		{
			value = ApplyCurve(value, pipeline.Curve);
		}
		return glm::length(value);
	}
}

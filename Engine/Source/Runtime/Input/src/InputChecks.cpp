#include "InputChecks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Nyx::Input
{
	namespace
	{
		// The binding's controls that are known: its control, or the composite's
		std::vector<ControlId> ControlsOf(const InputBindingData& binding)
		{
			std::vector<ControlId> controls;
			const auto add = [&controls](const std::string& path)
			{
				if (!path.empty())
				{
					if (const std::optional<ControlId> control = ParseControlPath(path))
					{
						controls.push_back(*control);
					}
				}
			};

			if (!binding.Control.empty())
			{
				add(binding.Control);
				return controls;
			}
			const CompositeKeysData& keys = binding.Composite;
			for (const std::string* path : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
			{
				add(*path);
			}
			return controls;
		}

		bool Shares(const std::vector<ControlId>& a, const std::vector<ControlId>& b)
		{
			return std::any_of(a.begin(), a.end(),
				[&b](ControlId control)
				{
					return std::find(b.begin(), b.end(), control) != b.end();
				});
		}

		bool SameModifiers(const InputBindingData& a, const InputBindingData& b)
		{
			return a.bCtrl == b.bCtrl && a.bShift == b.bShift && a.bAlt == b.bAlt && a.bSuper == b.bSuper;
		}

		bool IsStick(ControlId control)
		{
			return control == Gamepad(EGamepadControl::LeftStick) || control == Gamepad(EGamepadControl::RightStick);
		}

		const InputActionData* FindActionData(const InputAssetData& asset, const std::string& name)
		{
			const auto found = std::find_if(asset.Actions.begin(), asset.Actions.end(),
				[&name](const InputActionData& action)
				{
					return action.Name == name;
				});
			return found != asset.Actions.end() ? &*found : nullptr;
		}

		// Whether a binding anywhere drives the action with the mouse's movement: a look action
		bool IsMouseLookAction(const InputAssetData& asset, const std::string& action)
		{
			for (const InputContextData& context : asset.Contexts)
			{
				for (const InputBindingData& binding : context.Bindings)
				{
					if (binding.Action == action && ParseControlPath(binding.Control) == Mouse(EMouseControl::Delta))
					{
						return true;
					}
				}
			}
			return false;
		}

		std::string Number(float value)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%g", value);
			return text;
		}

		std::string Milliseconds(float seconds)
		{
			return std::to_string(static_cast<int>(std::lround(seconds * 1000.0f))) + " ms";
		}

		const char* DeviceWord(EDeviceKind device)
		{
			switch (device)
			{
			case EDeviceKind::Keyboard: return "keyboard";
			case EDeviceKind::Mouse:    return "mouse";
			case EDeviceKind::Gamepad:  return "pad";
			}
			return "device";
		}

		struct Checker
		{
			const InputAssetData& Asset;
			const InputCheckRuntime* Runtime = nullptr;
			std::vector<InputCheckFinding> Findings;

			void Add(EInputCheck check, size_t context, size_t binding, std::string message, std::optional<InputCheckFix> fix = std::nullopt)
			{
				InputCheckFinding finding;
				finding.Check = check;
				finding.Context = context;
				finding.Binding = binding;
				finding.Action = Asset.Contexts[context].Bindings[binding].Action;
				finding.Message = std::move(message);
				if (fix)
				{
					fix->Context = context;
					fix->Binding = binding;
				}
				finding.Fix = std::move(fix);
				Findings.push_back(std::move(finding));
			}

			void CheckBinding(size_t contextIndex, size_t bindingIndex)
			{
				const InputBindingData& binding = Asset.Contexts[contextIndex].Bindings[bindingIndex];
				const ValuePipeline& pipeline = binding.Pipeline;
				const std::vector<ControlId> controls = ControlsOf(binding);
				const InputActionData* action = FindActionData(Asset, binding.Action);
				const bool bSingleControl = !binding.Control.empty();

				if (bSingleControl && controls.size() == 1 && controls[0] == Mouse(EMouseControl::Delta) && pipeline.bMultiplyByDeltaTime)
				{
					Add(EInputCheck::DeltaTimeOnMouse, contextIndex, bindingIndex,
						"Mouse/Delta is a distance already: times the frame's time, aiming changes with the frame rate",
						InputCheckFix{ .Label = "Turn off Delta time", .Field = "Pipeline.bMultiplyByDeltaTime", .Value = 0.0f });
				}

				const bool bLooks = IsMouseLookAction(Asset, binding.Action) || pipeline.LookAcceleration.bEnabled;
				if (bSingleControl && controls.size() == 1 && IsStick(controls[0]) && bLooks && !pipeline.bMultiplyByDeltaTime)
				{
					Add(EInputCheck::NoDeltaTimeOnStickRate, contextIndex, bindingIndex,
						"A stick that turns is a rate: without the frame's time, it turns faster at higher frame rates",
						InputCheckFix{ .Label = "Turn on Delta time", .Field = "Pipeline.bMultiplyByDeltaTime", .Value = 1.0f });
				}

				if (!bSingleControl && !binding.Composite.bNormalize)
				{
					const CompositeKeysData& keys = binding.Composite;
					const int axes = (!keys.Right.empty() || !keys.Left.empty()) + (!keys.Up.empty() || !keys.Down.empty()) +
						(!keys.Forward.empty() || !keys.Back.empty());
					if (axes >= 2)
					{
						Add(EInputCheck::CompositeNotNormalized, contextIndex, bindingIndex,
							"Two keys at once (e.g. forward and right) give a longer vector than one: faster diagonally",
							InputCheckFix{ .Label = "Turn on Normalize", .Field = "Composite.bNormalize", .Value = 1.0f });
					}
				}

				const DeadZoneSettings& deadZone = pipeline.DeadZone;
				if (deadZone.bEnabled && deadZone.Inner >= deadZone.Outer)
				{
					Add(EInputCheck::DeadZone, contextIndex, bindingIndex,
						"Inner " + Number(deadZone.Inner) + " is at or past outer " + Number(deadZone.Outer) + ": the control jumps from 0 to full");
				}
				else if (deadZone.bEnabled && deadZone.Inner > 0.5f)
				{
					Add(EInputCheck::DeadZone, contextIndex, bindingIndex,
						"Inner " + Number(deadZone.Inner) + " ignores more than half of the travel");
				}

				if (pipeline.Smoothing.bEnabled && pipeline.Smoothing.TimeConstantSeconds > 0.03f)
				{
					Add(EInputCheck::SmoothingLatency, contextIndex, bindingIndex,
						"Smoothing of " + Milliseconds(pipeline.Smoothing.TimeConstantSeconds) + " adds about that much latency",
						InputCheckFix{ .Label = "Set to 30 ms", .Field = "Pipeline.Smoothing.TimeConstantSeconds", .Value = 0.03f });
				}

				// Analog controls go down and up at thresholds, for button actions and every trigger but Down
				const bool bAnalog = std::any_of(controls.begin(), controls.end(),
					[](ControlId control)
					{
						return !IsButton(control);
					});
				const bool bUsedAsButton = (action && action->ValueType == EActionValue::Button) || binding.Trigger.Kind != ETriggerKind::Down;
				if (bAnalog && bUsedAsButton && binding.Trigger.ReleaseThreshold >= binding.Trigger.PressThreshold)
				{
					Add(EInputCheck::ThresholdWithoutHysteresis, contextIndex, bindingIndex,
						"It lets go at " + Number(binding.Trigger.ReleaseThreshold) + ", not below where it presses (" +
							Number(binding.Trigger.PressThreshold) + "): held near there, it flickers",
						InputCheckFix{ .Label = "Let go 0.1 lower", .Field = "Trigger.ReleaseThreshold", .Value = binding.Trigger.PressThreshold - 0.1f });
				}

				if (Runtime && binding.Trigger.Kind == ETriggerKind::Down &&
					std::find(Runtime->ConsumedActions.begin(), Runtime->ConsumedActions.end(), binding.Action) != Runtime->ConsumedActions.end())
				{
					Add(EInputCheck::DownOnOneShot, contextIndex, bindingIndex,
						"The code takes " + binding.Action + " once per firing, but Down fires every frame it is held",
						InputCheckFix{ .Label = "Use Pressed", .Field = "Trigger.Kind", .Value = static_cast<float>(ETriggerKind::Pressed) });
				}

				CheckDeviceOnly(contextIndex, bindingIndex, controls);
			}

			// A control some models lack, with no other binding of the action here for them
			void CheckDeviceOnly(size_t contextIndex, size_t bindingIndex, const std::vector<ControlId>& controls)
			{
				const InputContextData& context = Asset.Contexts[contextIndex];
				const InputBindingData& binding = context.Bindings[bindingIndex];
				std::string missing;
				for (const EGamepadModel model : { EGamepadModel::XboxOne, EGamepadModel::DualShock4, EGamepadModel::DualSense })
				{
					const auto modelHas = [model](const std::vector<ControlId>& of)
					{
						return std::any_of(of.begin(), of.end(),
							[model](ControlId control)
							{
								return control.Device == EDeviceKind::Gamepad && ModelHasControl(model, static_cast<EGamepadControl>(control.Index));
							});
					};
					const bool bPadBinding = std::any_of(controls.begin(), controls.end(),
						[](ControlId control)
						{
							return control.Device == EDeviceKind::Gamepad;
						});
					if (!bPadBinding || modelHas(controls))
					{
						continue;
					}

					const bool bOther = std::any_of(context.Bindings.begin(), context.Bindings.end(),
						[&](const InputBindingData& other)
						{
							return &other != &binding && other.Action == binding.Action && modelHas(ControlsOf(other));
						});
					if (!bOther)
					{
						missing += std::string(missing.empty() ? "" : ", ") + GetGamepadModelName(model);
					}
				}
				if (!missing.empty())
				{
					Add(EInputCheck::DeviceOnlyControl, contextIndex, bindingIndex,
						"Not every pad has " + DescribeControls(binding) + ", and " + binding.Action + " has no other binding here for: " + missing);
				}
			}

			// Pairs of bindings in one context on the same control
			void CheckPairs(size_t contextIndex)
			{
				const InputContextData& context = Asset.Contexts[contextIndex];
				for (size_t first = 0; first < context.Bindings.size(); ++first)
				{
					const InputBindingData& a = context.Bindings[first];
					const std::vector<ControlId> aControls = ControlsOf(a);
					for (size_t second = first + 1; second < context.Bindings.size(); ++second)
					{
						const InputBindingData& b = context.Bindings[second];
						if (!SameModifiers(a, b) || !Shares(aControls, ControlsOf(b)))
						{
							continue;
						}

						const ETriggerKind aKind = a.Trigger.Kind;
						const ETriggerKind bKind = b.Trigger.Kind;
						const bool bTapHold = (aKind == ETriggerKind::Tap && bKind == ETriggerKind::Hold) ||
							(aKind == ETriggerKind::Hold && bKind == ETriggerKind::Tap);
						if (bTapHold)
						{
							const InputBindingData& tap = aKind == ETriggerKind::Tap ? a : b;
							const InputBindingData& hold = aKind == ETriggerKind::Hold ? a : b;
							const size_t holdIndex = aKind == ETriggerKind::Hold ? first : second;
							if (hold.Trigger.HoldSeconds <= tap.Trigger.TapMaxSeconds)
							{
								Add(EInputCheck::TapHoldOverlap, contextIndex, holdIndex,
									"The hold (" + Milliseconds(hold.Trigger.HoldSeconds) + ") fires before a tap (up to " +
										Milliseconds(tap.Trigger.TapMaxSeconds) + ") is over",
									InputCheckFix{ .Label = "Hold 100 ms longer than the tap",
										.Field = "Trigger.HoldSeconds",
										.Value = tap.Trigger.TapMaxSeconds + 0.1f });
							}
							continue;
						}

						if (a.Action != b.Action)
						{
							Add(EInputCheck::Conflict, contextIndex, second,
								DescribeControls(b) + " drives " + a.Action + " too (binding " + std::to_string(first) +
									"): both fire, without a tap/hold split between them");
						}
					}
				}
			}

			// Contexts that are on: a higher one that takes the binding's controls
			void CheckUnreachable()
			{
				if (!Runtime)
				{
					return;
				}

				const auto isActive = [this](const InputContextData& context)
				{
					return std::find(Runtime->ActiveContexts.begin(), Runtime->ActiveContexts.end(), context.Name) != Runtime->ActiveContexts.end();
				};

				for (size_t contextIndex = 0; contextIndex < Asset.Contexts.size(); ++contextIndex)
				{
					const InputContextData& context = Asset.Contexts[contextIndex];
					if (!isActive(context))
					{
						continue;
					}

					for (size_t bindingIndex = 0; bindingIndex < context.Bindings.size(); ++bindingIndex)
					{
						const std::vector<ControlId> controls = ControlsOf(context.Bindings[bindingIndex]);
						if (controls.empty())
						{
							continue;
						}

						for (const InputContextData& higher : Asset.Contexts)
						{
							if (&higher == &context || !isActive(higher) || higher.Priority <= context.Priority)
							{
								continue;
							}

							const auto takes = [&higher](ControlId control)
							{
								if ((control.Device == EDeviceKind::Keyboard && higher.bTakesKeyboard) ||
									(control.Device == EDeviceKind::Mouse && higher.bTakesMouse) ||
									(control.Device == EDeviceKind::Gamepad && higher.bTakesGamepad))
								{
									return true;
								}
								return std::any_of(higher.Bindings.begin(), higher.Bindings.end(),
									[control](const InputBindingData& binding)
									{
										const std::vector<ControlId> taken = ControlsOf(binding);
										return binding.bTakesControls && !binding.bCtrl && !binding.bShift && !binding.bAlt && !binding.bSuper &&
											std::find(taken.begin(), taken.end(), control) != taken.end();
									});
							};
							if (std::all_of(controls.begin(), controls.end(), takes))
							{
								const bool bWhole = std::any_of(controls.begin(), controls.end(),
									[&higher](ControlId control)
									{
										return (control.Device == EDeviceKind::Keyboard && higher.bTakesKeyboard) ||
											(control.Device == EDeviceKind::Mouse && higher.bTakesMouse) ||
											(control.Device == EDeviceKind::Gamepad && higher.bTakesGamepad);
									});
								Add(EInputCheck::Unreachable, contextIndex, bindingIndex,
									"Never reaches " + context.Bindings[bindingIndex].Action + " now: " + higher.Name + " (" +
										std::to_string(higher.Priority) + ") is on and takes " +
										(bWhole ? std::string("the whole ") + DeviceWord(controls[0].Device) : DescribeControls(context.Bindings[bindingIndex])));
								break;
							}
						}
					}
				}
			}

			void CheckCoverage()
			{
				for (const CoverageRow& row : ComputeCoverage(Asset))
				{
					const bool bKeyboardMouse = row.bCovered[static_cast<size_t>(ECoverageColumn::KeyboardMouse)];
					const bool bAnyPad = row.bCovered[static_cast<size_t>(ECoverageColumn::Xbox)] ||
						row.bCovered[static_cast<size_t>(ECoverageColumn::DualShock4)] || row.bCovered[static_cast<size_t>(ECoverageColumn::DualSense)];

					std::string missing;
					if (row.IsExpected(ECoverageColumn::KeyboardMouse) && !bKeyboardMouse)
					{
						missing = "keyboard and mouse";
					}
					if (row.Devices != EActionDevices::KeyboardMouse && !bAnyPad)
					{
						missing += std::string(missing.empty() ? "" : " or ") + "pads";
					}
					if (!missing.empty())
					{
						InputCheckFinding finding;
						finding.Check = EInputCheck::Coverage;
						finding.Action = row.Action;
						finding.Message = row.Action + " has no binding for " + missing;
						Findings.push_back(std::move(finding));
					}
				}
			}
		};
	}

	const char* GetInputCheckName(EInputCheck check)
	{
		switch (check)
		{
		case EInputCheck::Conflict:                   return "Conflict";
		case EInputCheck::Unreachable:                return "Unreachable";
		case EInputCheck::Coverage:                   return "Coverage";
		case EInputCheck::DownOnOneShot:              return "Down on a one-shot";
		case EInputCheck::DeltaTimeOnMouse:           return "Delta time on mouse delta";
		case EInputCheck::NoDeltaTimeOnStickRate:     return "No delta time on a stick rate";
		case EInputCheck::CompositeNotNormalized:     return "Composite not normalized";
		case EInputCheck::DeadZone:                   return "Dead zone";
		case EInputCheck::TapHoldOverlap:             return "Tap/hold overlap";
		case EInputCheck::SmoothingLatency:           return "Smoothing latency";
		case EInputCheck::ThresholdWithoutHysteresis: return "Threshold without hysteresis";
		case EInputCheck::DeviceOnlyControl:          return "Device-only control";
		case EInputCheck::Count:                      break;
		}
		return "Unknown";
	}

	InputCheckRuntime MakeCheckRuntime(const InputSystem& input)
	{
		InputCheckRuntime runtime;
		const InputUser& user = input.GetUser();
		for (const InputUser::StackEntry& entry : user.GetStack())
		{
			runtime.ActiveContexts.push_back(input.GetContext(entry.Context).Name);
		}
		for (uint32_t index = 0; index < static_cast<uint32_t>(input.GetActionCount()); ++index)
		{
			const ActionHandle action = ActionHandle::Make(index, 1);
			if (user.WasEverConsumed(action))
			{
				runtime.ConsumedActions.push_back(input.GetAction(action).Name);
			}
		}
		return runtime;
	}

	std::vector<InputCheckFinding> RunInputChecks(const InputAssetData& asset, const InputCheckRuntime* runtime)
	{
		Checker checker{ asset, runtime };
		for (size_t contextIndex = 0; contextIndex < asset.Contexts.size(); ++contextIndex)
		{
			for (size_t bindingIndex = 0; bindingIndex < asset.Contexts[contextIndex].Bindings.size(); ++bindingIndex)
			{
				checker.CheckBinding(contextIndex, bindingIndex);
			}
			checker.CheckPairs(contextIndex);
		}
		checker.CheckUnreachable();
		checker.CheckCoverage();
		return std::move(checker.Findings);
	}

	std::vector<const InputCheckFinding*> FindingsFor(const std::vector<InputCheckFinding>& findings, size_t context, size_t binding)
	{
		std::vector<const InputCheckFinding*> found;
		for (const InputCheckFinding& finding : findings)
		{
			if (finding.Context == context && finding.Binding == binding)
			{
				found.push_back(&finding);
			}
		}
		return found;
	}

	bool ApplyFix(InputAssetData& asset, const InputCheckFix& fix)
	{
		if (fix.Context >= asset.Contexts.size() || fix.Binding >= asset.Contexts[fix.Context].Bindings.size())
		{
			return false;
		}
		return ApplyOverride(asset.Contexts[fix.Context].Bindings[fix.Binding], { .Field = fix.Field, .Value = fix.Value });
	}

	std::string DescribeWhere(const InputAssetData& asset, const InputCheckFinding& finding)
	{
		if (!finding.Context || !finding.Binding || *finding.Context >= asset.Contexts.size() ||
			*finding.Binding >= asset.Contexts[*finding.Context].Bindings.size())
		{
			return "action " + finding.Action;
		}
		const InputContextData& context = asset.Contexts[*finding.Context];
		const InputBindingData& binding = context.Bindings[*finding.Binding];
		return context.Name + ", binding " + std::to_string(*finding.Binding) + " (" + binding.Action + ": " + DescribeControls(binding) + ")";
	}

	std::string DescribeControls(const InputBindingData& binding)
	{
		if (!binding.Control.empty())
		{
			return binding.Control;
		}
		std::string text;
		const CompositeKeysData& keys = binding.Composite;
		for (const std::string* path : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
		{
			if (!path->empty())
			{
				text += (text.empty() ? "" : ", ") + *path;
			}
		}
		return text.empty() ? "no control" : text;
	}

	bool ModelHasControl(EGamepadModel model, EGamepadControl control)
	{
		const bool bPlayStation = model == EGamepadModel::DualShock4 || model == EGamepadModel::DualSense || model == EGamepadModel::DualSenseEdge;
		switch (control)
		{
		// The touchpad and the motion sensors
		case EGamepadControl::TouchpadButton:
		case EGamepadControl::Touchpad0:
		case EGamepadControl::Touchpad1:
		case EGamepadControl::Gyro:
		case EGamepadControl::Accel:
			return bPlayStation;
		// The mute button
		case EGamepadControl::Misc:
			return model == EGamepadModel::DualSense || model == EGamepadModel::DualSenseEdge;
		// The Edge's back buttons; Xbox Elite pads have paddles too, but not every Xbox pad
		case EGamepadControl::LeftPaddle1:
		case EGamepadControl::RightPaddle1:
			return model == EGamepadModel::DualSenseEdge;
		case EGamepadControl::LeftPaddle2:
		case EGamepadControl::RightPaddle2:
			return false;
		default:
			return control < EGamepadControl::Count;
		}
	}

	const char* GetCoverageColumnName(ECoverageColumn column)
	{
		switch (column)
		{
		case ECoverageColumn::KeyboardMouse: return "Keyboard+Mouse";
		case ECoverageColumn::Xbox:          return "Xbox";
		case ECoverageColumn::DualShock4:    return "DualShock 4";
		case ECoverageColumn::DualSense:     return "DualSense";
		case ECoverageColumn::Count:         break;
		}
		return "Unknown";
	}

	bool CoverageRow::IsExpected(ECoverageColumn column) const
	{
		return column == ECoverageColumn::KeyboardMouse ? Devices != EActionDevices::Gamepad : Devices != EActionDevices::KeyboardMouse;
	}

	bool CoverageRow::HasGap() const
	{
		for (size_t column = 0; column < bCovered.size(); ++column)
		{
			if (IsExpected(static_cast<ECoverageColumn>(column)) && !bCovered[column])
			{
				return true;
			}
		}
		return false;
	}

	std::vector<CoverageRow> ComputeCoverage(const InputAssetData& asset)
	{
		std::vector<CoverageRow> rows;
		for (const InputActionData& action : asset.Actions)
		{
			CoverageRow row;
			row.Action = action.Name;
			row.Devices = action.Devices;
			for (const InputContextData& context : asset.Contexts)
			{
				for (const InputBindingData& binding : context.Bindings)
				{
					if (binding.Action != action.Name)
					{
						continue;
					}
					for (const ControlId control : ControlsOf(binding))
					{
						if (control.Device != EDeviceKind::Gamepad)
						{
							row.bCovered[static_cast<size_t>(ECoverageColumn::KeyboardMouse)] = true;
							continue;
						}
						const EGamepadControl padControl = static_cast<EGamepadControl>(control.Index);
						row.bCovered[static_cast<size_t>(ECoverageColumn::Xbox)] |= ModelHasControl(EGamepadModel::XboxOne, padControl);
						row.bCovered[static_cast<size_t>(ECoverageColumn::DualShock4)] |= ModelHasControl(EGamepadModel::DualShock4, padControl);
						row.bCovered[static_cast<size_t>(ECoverageColumn::DualSense)] |= ModelHasControl(EGamepadModel::DualSense, padControl);
					}
				}
			}
			rows.push_back(row);
		}
		return rows;
	}
}

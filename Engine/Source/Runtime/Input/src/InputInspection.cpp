#include "InputInspection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Nyx::Input
{
	namespace
	{
		bool Reads(const InputBinding& binding, ControlId control)
		{
			const std::vector<ControlId> controls = GetBindingControls(binding);
			return std::find(controls.begin(), controls.end(), control) != controls.end();
		}

		std::string Format(const char* format, double a, double b = 0.0)
		{
			char text[160];
			std::snprintf(text, sizeof(text), format, a, b);
			return text;
		}
	}

	std::vector<ControlId> GetBindingControls(const InputBinding& binding)
	{
		std::vector<ControlId> controls;
		if (binding.Control)
		{
			controls.push_back(*binding.Control);
		}
		if (binding.Composite)
		{
			const CompositeKeys& keys = *binding.Composite;
			for (const std::optional<ControlId>* key : { &keys.Right, &keys.Left, &keys.Up, &keys.Down, &keys.Forward, &keys.Back })
			{
				if (*key)
				{
					controls.push_back(**key);
				}
			}
		}
		return controls;
	}

	std::vector<ControlUse> ResolveControl(const InputSystem& input, std::span<const InputUser::StackEntry> stack, ControlId control)
	{
		std::vector<ControlUse> uses;
		for (const InputUser::StackEntry& entry : stack)
		{
			const InputContextDef& context = input.GetContext(entry.Context);
			bool bTaken = false;
			for (uint32_t index = 0; index < context.Bindings.size(); ++index)
			{
				const InputBinding& binding = context.Bindings[index];
				if (!Reads(binding, control))
				{
					continue;
				}
				const bool bTakes = binding.bTakesControls && binding.Modifiers == EModifierKeys::None;
				uses.push_back({ entry.Context, index, binding.Action, bTakes });
				bTaken = bTaken || bTakes;
			}

			// The context's bindings don't take from each other; the contexts below get nothing
			const uint32_t takesDevices = entry.Activation.TakesDevices.value_or(context.TakesDevices);
			if (!bTaken && (takesDevices & DeviceBit(control.Device)) != 0)
			{
				uses.push_back({ entry.Context, NoBinding, ActionHandle{}, true });
				bTaken = true;
			}
			if (bTaken)
			{
				break;
			}
		}
		return uses;
	}

	std::vector<ControlUse> FindBindingsOf(const InputSystem& input, ControlId control)
	{
		std::vector<ControlUse> uses;
		for (uint32_t contextIndex = 0; contextIndex < input.GetContextCount(); ++contextIndex)
		{
			const ContextHandle context = ContextHandle::Make(contextIndex, 1);
			const InputContextDef& def = input.GetContext(context);
			for (uint32_t index = 0; index < def.Bindings.size(); ++index)
			{
				const InputBinding& binding = def.Bindings[index];
				if (Reads(binding, control))
				{
					uses.push_back({ context, index, binding.Action, binding.bTakesControls && binding.Modifiers == EModifierKeys::None });
				}
			}
		}
		return uses;
	}

	std::vector<InputUser::StackEntry> MakeStack(const InputSystem& input, std::span<const ContextHandle> contexts)
	{
		std::vector<InputUser::StackEntry> stack;
		for (const ContextHandle context : contexts)
		{
			stack.push_back({ context, ContextActivation{ .Reason = "What if" } });
		}
		std::stable_sort(stack.begin(), stack.end(),
			[&input](const InputUser::StackEntry& a, const InputUser::StackEntry& b)
			{
				return input.GetContext(a.Context).Priority > input.GetContext(b.Context).Priority;
			});
		return stack;
	}

	void StickCalibration::Start(EMeasure measure, InputDeviceId device, ControlId stick, uint64_t nowUs, float seconds)
	{
		bRunning = true;
		Measure = measure;
		Device = device;
		Stick = stick;
		StartUs = nowUs;
		EndUs = nowUs + static_cast<uint64_t>(seconds * 1'000'000.0f);
		Samples.clear();
	}

	void StickCalibration::Cancel()
	{
		bRunning = false;
		Samples.clear();
	}

	void StickCalibration::Sample(const InputFrame& frame)
	{
		if (!bRunning)
		{
			return;
		}
		if (!frame.FindDevice(Device))
		{
			Cancel();
			LastResult = Result{ Measure, std::nullopt, "The pad was disconnected" };
			return;
		}

		Samples.push_back(glm::vec2(frame.GetState(Device, Stick).Value));
		if (frame.GetTimeUs() >= EndUs)
		{
			Finish();
		}
	}

	float StickCalibration::GetProgress(uint64_t nowUs) const
	{
		if (!bRunning || EndUs <= StartUs)
		{
			return 0.0f;
		}
		return std::clamp(static_cast<float>(nowUs - StartUs) / static_cast<float>(EndUs - StartUs), 0.0f, 1.0f);
	}

	void StickCalibration::Finish()
	{
		bRunning = false;
		LastResult = Measure == EMeasure::Drift ? SuggestInnerDeadZone(Samples) : SuggestOuterDeadZone(Samples);
	}

	StickCalibration::Result StickCalibration::SuggestInnerDeadZone(std::span<const glm::vec2> samples)
	{
		Result result{ EMeasure::Drift };
		if (samples.empty())
		{
			result.Explanation = "No samples";
			return result;
		}

		float farthest = 0.0f;
		for (const glm::vec2& sample : samples)
		{
			farthest = std::max(farthest, glm::length(sample));
		}
		const float withMargin = std::ceil((farthest * 1.2f + 0.02f) * 100.0f - 1e-3f) / 100.0f;
		result.Suggested = std::max(withMargin, 0.05f);
		result.Explanation = Format("Rests up to %.3f from the middle; with a margin: %.2f", farthest, *result.Suggested);
		return result;
	}

	StickCalibration::Result StickCalibration::SuggestOuterDeadZone(std::span<const glm::vec2> samples)
	{
		Result result{ EMeasure::Range };
		constexpr float Pi = 3.14159265358979f;

		// Only pushes towards the edge count for a direction
		std::array<float, RangeSectors> reach{};
		for (const glm::vec2& sample : samples)
		{
			const float length = glm::length(sample);
			if (length < 0.5f)
			{
				continue;
			}
			const float angle = std::atan2(sample.y, sample.x) + Pi;
			const int sector = std::min(static_cast<int>(angle / (2.0f * Pi) * RangeSectors), RangeSectors - 1);
			reach[sector] = std::max(reach[sector], length);
		}

		int reached = 0;
		float shortest = 1.0f;
		for (const float length : reach)
		{
			if (length > 0.0f)
			{
				++reached;
				shortest = std::min(shortest, length);
			}
		}

		if (reached < RangeSectorsNeeded)
		{
			result.Explanation = Format("Reached %.0f of %.0f directions: turn the stick all the way round, pushed to the edge", reached, RangeSectors);
			return result;
		}

		result.Suggested = std::floor((shortest - 0.01f) * 100.0f + 1e-3f) / 100.0f;
		result.Explanation = Format("The edge is %.3f from the middle where it is closest; less a margin: %.2f", shortest, *result.Suggested);
		return result;
	}
}

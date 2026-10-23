#include "InputOutput.h"

#include <algorithm>
#include <cstdio>

namespace Nyx::Input
{
	namespace
	{
		uint8_t ToByte(float value)
		{
			return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		std::string Format(const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0)
		{
			char text[128];
			std::snprintf(text, sizeof(text), format, a, b, c, d);
			return text;
		}

		// Where in SDL's DS5EffectsState_t the parts are
		constexpr size_t EnableBits1Offset = 0;
		constexpr size_t RightTriggerOffset = 10;
		constexpr size_t LeftTriggerOffset = 21;
		constexpr uint8_t EnableRightTrigger = 0x04;
		constexpr uint8_t EnableLeftTrigger = 0x08;
	}

	const char* GetTriggerEffectKindName(ETriggerEffectKind kind)
	{
		switch (kind)
		{
		case ETriggerEffectKind::Off:        return "Off";
		case ETriggerEffectKind::Resistance: return "Resistance";
		case ETriggerEffectKind::Weapon:     return "Weapon";
		case ETriggerEffectKind::Vibration:  return "Vibration";
		}
		return "Unknown";
	}

	bool DeviceOutput::IsEmpty() const
	{
		return !Rumble && !TriggerRumble && !LeftTriggerEffect && !RightTriggerEffect && !LightColor && !PlayerIndex;
	}

	void DeviceOutput::MergeFrom(const DeviceOutput& other)
	{
		if (other.Rumble)
		{
			RumbleOutput merged = Rumble.value_or(RumbleOutput{});
			merged.Low = std::max(merged.Low, other.Rumble->Low);
			merged.High = std::max(merged.High, other.Rumble->High);
			merged.DurationMs = std::max(merged.DurationMs, other.Rumble->DurationMs);
			Rumble = merged;
		}
		if (other.TriggerRumble)
		{
			TriggerRumbleOutput merged = TriggerRumble.value_or(TriggerRumbleOutput{});
			merged.Left = std::max(merged.Left, other.TriggerRumble->Left);
			merged.Right = std::max(merged.Right, other.TriggerRumble->Right);
			merged.DurationMs = std::max(merged.DurationMs, other.TriggerRumble->DurationMs);
			TriggerRumble = merged;
		}
		LeftTriggerEffect = other.LeftTriggerEffect ? other.LeftTriggerEffect : LeftTriggerEffect;
		RightTriggerEffect = other.RightTriggerEffect ? other.RightTriggerEffect : RightTriggerEffect;
		LightColor = other.LightColor ? other.LightColor : LightColor;
		PlayerIndex = other.PlayerIndex ? other.PlayerIndex : PlayerIndex;
	}

	std::string DescribeRumble(const RumbleOutput& rumble)
	{
		return Format("Rumble %.2g/%.2g for %.0f ms", rumble.Low, rumble.High, rumble.DurationMs);
	}

	std::string DescribeTriggerRumble(const TriggerRumbleOutput& rumble)
	{
		return Format("Trigger rumble %.2g/%.2g for %.0f ms", rumble.Left, rumble.Right, rumble.DurationMs);
	}

	std::string DescribeTriggerEffect(ETrigger trigger, const TriggerEffect& effect)
	{
		std::string text = std::string("Trigger effect ") + (trigger == ETrigger::Left ? "left: " : "right: ") +
			GetTriggerEffectKindName(effect.Kind);
		switch (effect.Kind)
		{
		case ETriggerEffectKind::Off:        break;
		case ETriggerEffectKind::Resistance: text += Format(" from %.2g, strength %.2g", effect.Start, effect.Strength); break;
		case ETriggerEffectKind::Weapon:     text += Format(" %.2g to %.2g, strength %.2g", effect.Start, effect.End, effect.Strength); break;
		case ETriggerEffectKind::Vibration:
			text += Format(" from %.2g, strength %.2g, %.0f Hz", effect.Start, effect.Strength, effect.FrequencyHz);
			break;
		}
		return text;
	}

	std::string DescribeLightColor(const glm::vec3& color)
	{
		return Format("Light (%.2g, %.2g, %.2g)", color.r, color.g, color.b);
	}

	std::array<uint8_t, 11> EncodeDualSenseTriggerEffect(const TriggerEffect& effect)
	{
		std::array<uint8_t, 11> bytes{};
		switch (effect.Kind)
		{
		case ETriggerEffectKind::Off:
			bytes[0] = 0x05;
			break;
		case ETriggerEffectKind::Resistance:
			bytes[0] = 0x01;
			bytes[1] = ToByte(effect.Start);
			bytes[2] = ToByte(effect.Strength);
			break;
		case ETriggerEffectKind::Weapon:
			bytes[0] = 0x02;
			bytes[1] = ToByte(effect.Start);
			bytes[2] = ToByte(effect.End);
			bytes[3] = ToByte(effect.Strength);
			break;
		case ETriggerEffectKind::Vibration:
			bytes[0] = 0x06;
			bytes[1] = static_cast<uint8_t>(std::clamp(effect.FrequencyHz, 0.0f, 255.0f) + 0.5f);
			bytes[2] = ToByte(effect.Strength);
			bytes[3] = ToByte(effect.Start);
			break;
		}
		return bytes;
	}

	std::array<uint8_t, 47> MakeDualSenseTriggerReport(const std::optional<TriggerEffect>& left, const std::optional<TriggerEffect>& right)
	{
		std::array<uint8_t, 47> report{};
		if (right)
		{
			report[EnableBits1Offset] |= EnableRightTrigger;
			const std::array<uint8_t, 11> bytes = EncodeDualSenseTriggerEffect(*right);
			std::copy(bytes.begin(), bytes.end(), report.begin() + RightTriggerOffset);
		}
		if (left)
		{
			report[EnableBits1Offset] |= EnableLeftTrigger;
			const std::array<uint8_t, 11> bytes = EncodeDualSenseTriggerEffect(*left);
			std::copy(bytes.begin(), bytes.end(), report.begin() + LeftTriggerOffset);
		}
		return report;
	}
}

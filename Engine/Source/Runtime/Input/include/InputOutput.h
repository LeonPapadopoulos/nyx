#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

// What the input system sends to devices: rumble, trigger rumble, the DualSense's trigger effects,
// a light colour, player LEDs. A device without the capability leaves it out.
namespace Nyx::Input
{
	enum class ETrigger : uint8_t
	{
		Left,
		Right,
	};

	struct RumbleOutput
	{
		// 0 to 1: the low-frequency (left, heavy) and high-frequency (right, light) motor
		float Low = 0.0f;
		float High = 0.0f;
		// How long; 0 stops it
		uint32_t DurationMs = 0;
	};

	// Motors in the triggers: Xbox impulse triggers, DualSense
	struct TriggerRumbleOutput
	{
		float Left = 0.0f;
		float Right = 0.0f;
		uint32_t DurationMs = 0;
	};

	// The DualSense's adaptive triggers, in its simple modes (as SDL's testcontroller uses them).
	// Positions are 0 (released) to 1 (fully pulled).
	enum class ETriggerEffectKind : uint8_t
	{
		Off,
		Resistance, // Pushes back from Start on, with Strength
		Weapon,     // Pushes back between Start and End, then gives way, like a trigger that breaks
		Vibration,  // Vibrates from Start on, with Strength as amplitude, at FrequencyHz
	};

	const char* GetTriggerEffectKindName(ETriggerEffectKind kind);

	struct TriggerEffect
	{
		ETriggerEffectKind Kind = ETriggerEffectKind::Off;
		float Start = 0.0f;
		float End = 1.0f;
		float Strength = 1.0f;
		float FrequencyHz = 0.0f;
	};

	// What one frame asks of one device. Several asks in one frame are merged (MergeFrom).
	struct DeviceOutput
	{
		std::optional<RumbleOutput> Rumble;
		std::optional<TriggerRumbleOutput> TriggerRumble;
		std::optional<TriggerEffect> LeftTriggerEffect;
		std::optional<TriggerEffect> RightTriggerEffect;
		// 0 to 1 per channel
		std::optional<glm::vec3> LightColor;
		// 0 for the first player; -1 turns the player LEDs off
		std::optional<int> PlayerIndex;

		bool IsEmpty() const;

		// Rumble: the stronger of each motor and the longer duration. Effects, light and player:
		// the later one.
		void MergeFrom(const DeviceOutput& other);
	};

	// Text for the log and the debug window, e.g. "Rumble 0.6/0.2 for 120 ms"
	std::string DescribeRumble(const RumbleOutput& rumble);
	std::string DescribeTriggerRumble(const TriggerRumbleOutput& rumble);
	std::string DescribeTriggerEffect(ETrigger trigger, const TriggerEffect& effect);
	std::string DescribeLightColor(const glm::vec3& color);

	// The DualSense's 11 bytes for one trigger's effect
	std::array<uint8_t, 11> EncodeDualSenseTriggerEffect(const TriggerEffect& effect);

	// What SDL_SendGamepadEffect() takes for a DualSense: its effects block (SDL's
	// DS5EffectsState_t, 47 bytes), changing only the triggers given
	std::array<uint8_t, 47> MakeDualSenseTriggerReport(const std::optional<TriggerEffect>& left, const std::optional<TriggerEffect>& right);
}

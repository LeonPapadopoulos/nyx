#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// What a binding does to a control's value before it reaches its action: a fixed order of steps,
// each of which can be switched off but not moved. The order "dead zone, curve, sensitivity" is
// the one that is right nearly always, and a fixed order gives traces the same columns every time.
namespace Nyx::Input
{
	enum class EPipelineStep : uint8_t
	{
		DeadZone,
		Curve,
		Sensitivity,
		LookAcceleration,
		Smoothing,
		DeltaTime,
		Count
	};

	inline constexpr size_t PipelineStepCount = static_cast<size_t>(EPipelineStep::Count);

	// "dead zone", "curve", ...
	const char* GetPipelineStepName(EPipelineStep step);

	enum class EDeadZoneShape : uint8_t
	{
		// Each axis on its own: |x| below Inner is 0, from Inner to Outer it rises from 0 to 1
		Axial,
		// The whole vector: shorter than Inner is 0, longer is kept as it is (up to length 1 at Outer)
		Radial,
		// The whole vector: its length rises from 0 at Inner to 1 at Outer, its direction is kept
		ScaledRadial,
		// Axes below Inner are dropped first (a push straight up stays straight up), then scaled radial
		Hybrid,
	};

	const char* GetDeadZoneShapeName(EDeadZoneShape shape);

	struct DeadZoneSettings
	{
		bool bEnabled = false;
		EDeadZoneShape Shape = EDeadZoneShape::ScaledRadial;
		float Inner = 0.1f;
		float Outer = 0.95f;
	};

	enum class ECurveKind : uint8_t
	{
		Linear,
		// length^Exponent: above 1, small movements become finer
		Power,
		// Straight lines between Points (x: length in, y: length out), sorted by x
		Custom,
	};

	const char* GetCurveKindName(ECurveKind kind);

	// Shapes the vector's length; its direction is kept
	struct CurveSettings
	{
		bool bEnabled = false;
		ECurveKind Kind = ECurveKind::Linear;
		float Exponent = 2.0f;
		std::vector<glm::vec2> Points;
	};

	struct SensitivitySettings
	{
		bool bEnabled = false;
		glm::vec3 Scale{ 1.0f };
		bool bInvertX = false;
		bool bInvertY = false;
	};

	// For sticks that turn a camera: once the stick has been past Threshold for DelaySeconds, the
	// value grows to MaxMultiplier times over RampSeconds, until the stick comes back.
	struct LookAccelerationSettings
	{
		bool bEnabled = false;
		float Threshold = 0.9f;
		float DelaySeconds = 0.25f;
		float RampSeconds = 0.5f;
		float MaxMultiplier = 2.0f;
	};

	// Follows the value with a delay of about TimeConstantSeconds. It adds that much latency.
	struct SmoothingSettings
	{
		bool bEnabled = false;
		float TimeConstantSeconds = 0.03f;
	};

	struct ValuePipeline
	{
		DeadZoneSettings DeadZone;
		CurveSettings Curve;
		SensitivitySettings Sensitivity;
		LookAccelerationSettings LookAcceleration;
		SmoothingSettings Smoothing;

		// Multiplies by the frame's time, for values that are rates (a stick turning a camera).
		// Not for Mouse/Delta: that is a distance already.
		bool bMultiplyByDeltaTime = false;
	};

	// What the stateful steps remember from frame to frame, per binding
	struct PipelineState
	{
		float SecondsAboveThreshold = 0.0f;
		glm::vec3 Smoothed{ 0.0f };
	};

	struct PipelineResult
	{
		glm::vec3 Value{ 0.0f };

		// The value after each step that ran; empty for steps that are off
		std::array<std::optional<glm::vec3>, PipelineStepCount> AfterStep;
	};

	glm::vec3 ApplyDeadZone(const glm::vec3& value, const DeadZoneSettings& settings);
	glm::vec3 ApplyCurve(const glm::vec3& value, const CurveSettings& settings);

	PipelineResult RunPipeline(const glm::vec3& raw, const ValuePipeline& pipeline, PipelineState& state, float deltaSeconds);
}

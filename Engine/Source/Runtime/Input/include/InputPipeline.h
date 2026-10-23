#pragma once

#include "ReflectionMacros.h"

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

	NYX_ENUM()
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

	NYX_REFLECT()
	struct DeadZoneSettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bEnabled = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		EDeadZoneShape Shape = EDeadZoneShape::ScaledRadial;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Inner = 0.1f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Outer = 0.95f;
	};

	NYX_ENUM()
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
	NYX_REFLECT()
	struct CurveSettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bEnabled = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		ECurveKind Kind = ECurveKind::Linear;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Exponent = 2.0f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<glm::vec2> Points;
	};

	NYX_REFLECT()
	struct SensitivitySettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bEnabled = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		glm::vec3 Scale{ 1.0f };
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bInvertX = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bInvertY = false;
	};

	// For sticks that turn a camera: once the stick has been past Threshold for DelaySeconds, the
	// value grows to MaxMultiplier times over RampSeconds, until the stick comes back.
	NYX_REFLECT()
	struct LookAccelerationSettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bEnabled = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Threshold = 0.9f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float DelaySeconds = 0.25f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float RampSeconds = 0.5f;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float MaxMultiplier = 2.0f;
	};

	// Follows the value with a delay of about TimeConstantSeconds. It adds that much latency.
	NYX_REFLECT()
	struct SmoothingSettings
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bEnabled = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float TimeConstantSeconds = 0.03f;
	};

	NYX_REFLECT()
	struct ValuePipeline
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		DeadZoneSettings DeadZone;
		NYX_PROPERTY(Edit, Undo, Serialize)
		CurveSettings Curve;
		NYX_PROPERTY(Edit, Undo, Serialize)
		SensitivitySettings Sensitivity;
		NYX_PROPERTY(Edit, Undo, Serialize)
		LookAccelerationSettings LookAcceleration;
		NYX_PROPERTY(Edit, Undo, Serialize)
		SmoothingSettings Smoothing;

		// Multiplies by the frame's time, for values that are rates (a stick turning a camera).
		// Not for Mouse/Delta: that is a distance already.
		NYX_PROPERTY(Edit, Undo, Serialize)
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

#include "Generated/Runtime/InputPipeline.reflect.h"

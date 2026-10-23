#include "InputPipeline.h"

#include <algorithm>
#include <cmath>

namespace Nyx::Input
{
	namespace
	{
		// 0 at inner, 1 at outer
		float Remap(float length, float inner, float outer)
		{
			if (outer <= inner)
			{
				return length >= inner ? 1.0f : 0.0f;
			}
			return std::clamp((length - inner) / (outer - inner), 0.0f, 1.0f);
		}

		glm::vec3 WithLength(const glm::vec3& value, float length)
		{
			const float current = glm::length(value);
			return current > 0.0f ? value * (length / current) : glm::vec3(0.0f);
		}

		glm::vec3 ScaledRadial(const glm::vec3& value, const DeadZoneSettings& settings)
		{
			const float length = glm::length(value);
			if (length < settings.Inner)
			{
				return glm::vec3(0.0f);
			}
			return WithLength(value, Remap(length, settings.Inner, settings.Outer));
		}

		float CustomCurve(float length, const std::vector<glm::vec2>& points)
		{
			if (points.empty())
			{
				return length;
			}
			if (length <= points.front().x)
			{
				return points.front().y;
			}
			for (size_t i = 1; i < points.size(); ++i)
			{
				if (length <= points[i].x)
				{
					const glm::vec2& a = points[i - 1];
					const glm::vec2& b = points[i];
					const float t = b.x > a.x ? (length - a.x) / (b.x - a.x) : 1.0f;
					return a.y + (b.y - a.y) * t;
				}
			}
			return points.back().y;
		}
	}

	const char* GetPipelineStepName(EPipelineStep step)
	{
		switch (step)
		{
		case EPipelineStep::DeadZone:         return "dead zone";
		case EPipelineStep::Curve:            return "curve";
		case EPipelineStep::Sensitivity:      return "sensitivity";
		case EPipelineStep::LookAcceleration: return "look acceleration";
		case EPipelineStep::Smoothing:        return "smoothing";
		case EPipelineStep::DeltaTime:        return "delta time";
		case EPipelineStep::Count:            break;
		}
		return "unknown";
	}

	const char* GetDeadZoneShapeName(EDeadZoneShape shape)
	{
		switch (shape)
		{
		case EDeadZoneShape::Axial:        return "axial";
		case EDeadZoneShape::Radial:       return "radial";
		case EDeadZoneShape::ScaledRadial: return "scaled radial";
		case EDeadZoneShape::Hybrid:       return "hybrid";
		}
		return "unknown";
	}

	const char* GetCurveKindName(ECurveKind kind)
	{
		switch (kind)
		{
		case ECurveKind::Linear: return "linear";
		case ECurveKind::Power:  return "power";
		case ECurveKind::Custom: return "custom";
		}
		return "unknown";
	}

	glm::vec3 ApplyDeadZone(const glm::vec3& value, const DeadZoneSettings& settings)
	{
		switch (settings.Shape)
		{
		case EDeadZoneShape::Axial:
		{
			glm::vec3 result{ 0.0f };
			for (int axis = 0; axis < 3; ++axis)
			{
				const float magnitude = std::abs(value[axis]);
				if (magnitude >= settings.Inner)
				{
					result[axis] = std::copysign(Remap(magnitude, settings.Inner, settings.Outer), value[axis]);
				}
			}
			return result;
		}

		case EDeadZoneShape::Radial:
		{
			const float length = glm::length(value);
			if (length < settings.Inner)
			{
				return glm::vec3(0.0f);
			}
			return length >= settings.Outer ? WithLength(value, 1.0f) : value;
		}

		case EDeadZoneShape::ScaledRadial:
			return ScaledRadial(value, settings);

		case EDeadZoneShape::Hybrid:
		{
			glm::vec3 axial = value;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (std::abs(axial[axis]) < settings.Inner)
				{
					axial[axis] = 0.0f;
				}
			}
			return ScaledRadial(axial, settings);
		}
		}
		return value;
	}

	glm::vec3 ApplyCurve(const glm::vec3& value, const CurveSettings& settings)
	{
		const float length = glm::length(value);
		switch (settings.Kind)
		{
		case ECurveKind::Linear: return value;
		case ECurveKind::Power:  return WithLength(value, std::pow(length, settings.Exponent));
		case ECurveKind::Custom: return WithLength(value, CustomCurve(length, settings.Points));
		}
		return value;
	}

	PipelineResult RunPipeline(const glm::vec3& raw, const ValuePipeline& pipeline, PipelineState& state, float deltaSeconds)
	{
		PipelineResult result;
		glm::vec3 value = raw;
		const auto record = [&result, &value](EPipelineStep step)
		{
			result.AfterStep[static_cast<size_t>(step)] = value;
		};

		if (pipeline.DeadZone.bEnabled)
		{
			value = ApplyDeadZone(value, pipeline.DeadZone);
			record(EPipelineStep::DeadZone);
		}

		if (pipeline.Curve.bEnabled)
		{
			value = ApplyCurve(value, pipeline.Curve);
			record(EPipelineStep::Curve);
		}

		// Look acceleration compares the stick's travel, before sensitivity scales it
		const float travel = glm::length(value);

		if (pipeline.Sensitivity.bEnabled)
		{
			value *= pipeline.Sensitivity.Scale;
			value.x = pipeline.Sensitivity.bInvertX ? -value.x : value.x;
			value.y = pipeline.Sensitivity.bInvertY ? -value.y : value.y;
			record(EPipelineStep::Sensitivity);
		}

		if (pipeline.LookAcceleration.bEnabled)
		{
			const LookAccelerationSettings& settings = pipeline.LookAcceleration;
			state.SecondsAboveThreshold = travel >= settings.Threshold ? state.SecondsAboveThreshold + deltaSeconds : 0.0f;

			const float rampedSeconds = state.SecondsAboveThreshold - settings.DelaySeconds;
			const float ramp = settings.RampSeconds > 0.0f ? std::clamp(rampedSeconds / settings.RampSeconds, 0.0f, 1.0f)
														   : (rampedSeconds >= 0.0f ? 1.0f : 0.0f);
			value *= 1.0f + (settings.MaxMultiplier - 1.0f) * ramp;
			record(EPipelineStep::LookAcceleration);
		}

		if (pipeline.Smoothing.bEnabled)
		{
			const float timeConstant = pipeline.Smoothing.TimeConstantSeconds;
			const float follow = timeConstant > 0.0f ? 1.0f - std::exp(-deltaSeconds / timeConstant) : 1.0f;
			state.Smoothed += (value - state.Smoothed) * follow;
			value = state.Smoothed;
			record(EPipelineStep::Smoothing);
		}

		if (pipeline.bMultiplyByDeltaTime)
		{
			value *= deltaSeconds;
			record(EPipelineStep::DeltaTime);
		}

		result.Value = value;
		return result;
	}
}

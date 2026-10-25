#include "NyxPCH.h"
#include "TimeOfDay.h"

#include "DirectionalLightComponent.h"
#include "GuidComponent.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		// The path's frame in world space: where the sun rises, the side the path's axis points to
		// (with the up axis), and up. Before turning by the sunrise direction, east is +X, the axis's
		// side -Z.
		struct SunPathFrame
		{
			glm::vec3 East;
			glm::vec3 North;
			glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
			float SinTilt = 0.0f;
			float CosTilt = 1.0f;
		};

		SunPathFrame GetSunPathFrame(const TimeOfDayComponent& timeOfDay)
		{
			const glm::quat turn = glm::angleAxis(glm::radians(timeOfDay.SunriseDirectionDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
			const float tilt = glm::radians(std::clamp(timeOfDay.PathTiltDegrees, -90.0f, 90.0f));
			SunPathFrame frame;
			frame.East = turn * glm::vec3(1.0f, 0.0f, 0.0f);
			frame.North = turn * glm::vec3(0.0f, 0.0f, -1.0f);
			frame.SinTilt = std::sin(tilt);
			frame.CosTilt = std::cos(tilt);
			return frame;
		}

		// The sun's hour angle: 0 at noon, a quarter turn before it at 6, after it at 18
		float GetHourAngle(float hours)
		{
			return (WrapHours(hours) - 12.0f) * glm::two_pi<float>() / HoursPerDay;
		}
	}

	float WrapHours(double hours)
	{
		double wrapped = std::fmod(hours, static_cast<double>(HoursPerDay));
		if (wrapped < 0.0)
		{
			wrapped += HoursPerDay;
		}
		// A float just below 24 rounds up to it
		const float result = static_cast<float>(wrapped);
		return result >= HoursPerDay ? 0.0f : result;
	}

	glm::vec3 GetSunDirection(const TimeOfDayComponent& timeOfDay, float hours)
	{
		// Each day the sun turns once about the path's axis, which points to the path's side, raised
		// from the horizon by the tilt (a pole star's height is the latitude). Its noon is the tilt
		// from overhead, away from the axis's side.
		const SunPathFrame frame = GetSunPathFrame(timeOfDay);
		const float angle = GetHourAngle(hours);
		const glm::vec3 noon = frame.Up * frame.CosTilt - frame.North * frame.SinTilt;
		return glm::normalize(noon * std::cos(angle) - frame.East * std::sin(angle));
	}

	glm::quat GetSunRotation(const TimeOfDayComponent& timeOfDay, float hours)
	{
		// +Z towards the sun, so -Z, the light's forward axis, away from it; +X the path's axis,
		// which is at right angles to every direction of the path
		const SunPathFrame frame = GetSunPathFrame(timeOfDay);
		const glm::vec3 axis = glm::normalize(frame.North * frame.CosTilt + frame.Up * frame.SinTilt);
		const glm::vec3 towardsSun = GetSunDirection(timeOfDay, hours);
		const glm::vec3 y = glm::normalize(glm::cross(towardsSun, axis));
		return glm::normalize(glm::quat_cast(glm::mat3(axis, y, towardsSun)));
	}

	float GetSunHorizonFade(float sinElevation, float fadeDegrees)
	{
		const float fade = std::clamp(fadeDegrees, 0.0f, 20.0f);
		if (fade <= 0.0f)
		{
			return sinElevation >= 0.0f ? 1.0f : 0.0f;
		}
		const float below = std::sin(glm::radians(fade));
		const float t = std::clamp((sinElevation + below) / below, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	TimeOfDayComponent* FindTimeOfDay(Registry& world, Entity* owner)
	{
		TimeOfDayComponent* found = nullptr;
		world.Each<TimeOfDayComponent>([&](Entity entity, TimeOfDayComponent& timeOfDay)
			{
				if (!found)
				{
					found = &timeOfDay;
					if (owner)
					{
						*owner = entity;
					}
				}
			});
		return found;
	}

	const TimeOfDayComponent* FindTimeOfDay(const Registry& world, Entity* owner)
	{
		const TimeOfDayComponent* found = nullptr;
		world.Each<TimeOfDayComponent>([&](Entity entity, const TimeOfDayComponent& timeOfDay)
			{
				if (!found)
				{
					found = &timeOfDay;
					if (owner)
					{
						*owner = entity;
					}
				}
			});
		return found;
	}

	std::optional<Entity> FindTimeOfDaySun(const Registry& world, const TimeOfDayComponent& timeOfDay)
	{
		std::optional<Entity> sun;
		world.Each<DirectionalLightComponent>([&](Entity entity, const DirectionalLightComponent& light)
			{
				if (sun)
				{
					return;
				}
				if (timeOfDay.Sun.IsValid())
				{
					if (world.Has<GuidComponent>(entity) && world.Get<GuidComponent>(entity).Guid == timeOfDay.Sun)
					{
						sun = entity;
					}
				}
				else if (light.bPrimary)
				{
					sun = entity;
				}
			});
		return sun;
	}

	void ApplyTimeOfDay(Registry& world)
	{
		const TimeOfDayComponent* timeOfDay = FindTimeOfDay(world);
		if (!timeOfDay)
		{
			return;
		}
		const std::optional<Entity> sun = FindTimeOfDaySun(world, *timeOfDay);
		if (sun && world.Has<TransformComponent>(*sun))
		{
			world.Get<TransformComponent>(*sun).Rotation = GetSunRotation(*timeOfDay, timeOfDay->Hours);
		}
	}

	float GetTimeOfDayLightFade(const Registry& world, Entity light, const glm::vec3& towardsLight)
	{
		const TimeOfDayComponent* timeOfDay = FindTimeOfDay(world);
		if (!timeOfDay || FindTimeOfDaySun(world, *timeOfDay) != light)
		{
			return 1.0f;
		}
		return GetSunHorizonFade(towardsLight.y, timeOfDay->HorizonFadeDegrees);
	}
}

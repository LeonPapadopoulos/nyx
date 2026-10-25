#pragma once

#include "Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

namespace Nyx
{
	struct TimeOfDayComponent;

	// The time of day as plain math (Docs/Design/atmosphere.md): where the sun is at an hour of its
	// path, how its light fades below the horizon, and the world's sun turned to the world's time.
	// The sun's direction is the same everywhere in the world, at every height.

	inline constexpr float HoursPerDay = 24.0f;

	// `hours` wrapped into [0, 24): 25 is 1, -1 is 23
	float WrapHours(double hours);

	// The direction towards the sun at `hours` on the component's path, normalised, in world space
	// (y is the sine of its height above the horizon): on the horizon at the sunrise direction at 6,
	// highest at noon, on the horizon opposite at 18, lowest at midnight. With a tilt of 0 it is
	// overhead at noon and straight down at midnight.
	glm::vec3 GetSunDirection(const TimeOfDayComponent& timeOfDay, float hours);

	// The rotation that turns a directional light's forward axis (-Z) away from the sun at `hours`.
	// Its X axis is the axis the path turns about, the same all day, so the light turns smoothly
	// through noon and midnight and never rolls.
	glm::quat GetSunRotation(const TimeOfDayComponent& timeOfDay, float hours);

	// What the sun's light is multiplied by at its height, `sinElevation` (the sine of its height
	// above the horizon): 1 on and above the horizon, falling smoothly to 0 by `fadeDegrees` below
	// it (clamped to 0 to 20; at once for 0)
	float GetSunHorizonFade(float sinElevation, float fadeDegrees);

	// The world's first time of day; null without one
	TimeOfDayComponent* FindTimeOfDay(Registry& world, Entity* owner = nullptr);
	const TimeOfDayComponent* FindTimeOfDay(const Registry& world, Entity* owner = nullptr);

	// The entity the time of day turns: its Sun, if that entity has a directional light, or the
	// world's first primary directional light when it names none. Goes through the directional
	// lights only, not every entity.
	std::optional<Entity> FindTimeOfDaySun(const Registry& world, const TimeOfDayComponent& timeOfDay);

	// Turns the world's sun to the world's time of day; nothing without one. The editor and a game
	// call it each frame, so a time set in the Details panel, undone, or sent to a game as a live
	// edit turns the sun at once, also while the game is paused; a game's steps call it too, after
	// moving the time on (TimeOfDayClock). It sets the rotation of the sun's TransformComponent
	// only, if it has one: its position, its light and its shadows' settings are its own.
	void ApplyTimeOfDay(Registry& world);

	// What the light of `light`, shining from `towardsLight` (normalised, towards the light), is
	// multiplied by for the world's time of day: the horizon fade, if it is the time of day's sun;
	// else 1. The renderer asks as it reads the world's light, so a sun below the horizon lights
	// nothing from below, in the editor's views as in a game.
	float GetTimeOfDayLightFade(const Registry& world, Entity light, const glm::vec3& towardsLight);
}

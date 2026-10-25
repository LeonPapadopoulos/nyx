#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

namespace Nyx
{
	// The world's time of day, and the path the sun takes across the sky over a day: the time turns
	// the sun's entity, a directional light, so the sky, the fog and the shadows, which follow the
	// sun's direction, follow the time. While time runs, a game's world moves it on with its steps
	// (TimeOfDayClock, Sim), a day taking Day Length seconds of the world's time; the editor shows
	// the time as it is set. Below the horizon the sun's light fades out. The world's first such
	// component counts; without one, nothing turns the sun (TimeOfDay.h).
	NYX_REFLECT(Component, meta = (DisplayName = "Time Of Day"))
	struct TimeOfDayComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Time", DisplayName = "Time", DragSpeed = 0.02, Tooltip = "The time of day in hours, 0 to 24: 6 sunrise, 12 noon, 18 sunset, 0 midnight; past 24 it wraps"))
		float Hours = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Time", DisplayName = "Time Runs", Tooltip = "Whether a running game moves the time on with its steps; the editor shows it as set"))
		bool bTimeRuns = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Time", DisplayName = "Day Length", DragSpeed = 1.0, Tooltip = "How many seconds of the world's time a whole day takes while time runs, at least 1"))
		float DayLengthSeconds = 1200.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sun", DisplayName = "Sun", Tooltip = "The entity with the directional light the time turns; none: the world's first primary directional light"))
		EntityGuid Sun;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sun", DisplayName = "Path Tilt", DragSpeed = 0.2, Tooltip = "How far the sun's arc leans from overhead, in degrees, -90 to 90, like a latitude: 0 straight overhead at noon, 30 at noon 60 degrees up on the right of where it rises"))
		float PathTiltDegrees = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sun", DisplayName = "Sunrise Direction", DragSpeed = 0.5, Tooltip = "Where on the horizon the sun rises, in degrees round the up axis from +X towards -Z: 0 rises at +X and sets at -X"))
		float SunriseDirectionDegrees = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sun", DisplayName = "Horizon Fade", DragSpeed = 0.1, Tooltip = "How many degrees below the horizon the sun's light fades out over, 0 to 20; below that it gives no light and casts no shadows"))
		float HorizonFadeDegrees = 3.0f;
	};
}

#include "Generated/Runtime/TimeOfDayComponent.reflect.h"

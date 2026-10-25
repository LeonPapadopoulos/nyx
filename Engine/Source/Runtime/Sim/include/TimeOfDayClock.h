#pragma once

#include "Entity.h"
#include "FixedStepClock.h"

namespace Nyx
{
	// Moves the world's time of day (TimeOfDayComponent) on with the world's steps while its time
	// runs, and turns the sun to it (ApplyTimeOfDay()). The time after a step is the time it ran
	// from plus the world's time since, a day every Day Length seconds: a function of the two, not
	// of the frames, so a recording played again or another machine running the same steps gets
	// the same sun, and the time doesn't drift by adding a little each step. A time set from
	// outside (an edit, a live edit, a load), another day length, or time starting to run again
	// starts it running from there. GameWorld::Step() calls it before its systems.
	class TimeOfDayClock
	{
	public:
		void Step(Registry& world, const StepTime& step);

	private:
		// Whose time runs, from which time of day at which world time, at which day length
		bool bRunning = false;
		Entity Owner;
		double FromHours = 0.0;
		double FromSeconds = 0.0;
		float DayLengthSeconds = 0.0f;
		// The time the last step wrote: one that differs was set from outside since
		float WrittenHours = 0.0f;
	};
}

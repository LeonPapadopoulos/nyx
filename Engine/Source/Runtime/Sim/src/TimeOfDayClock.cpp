#include "NyxPCH.h"
#include "TimeOfDayClock.h"

#include "TimeOfDay.h"
#include "TimeOfDayComponent.h"

#include <algorithm>

namespace Nyx
{
	void TimeOfDayClock::Step(Registry& world, const StepTime& step)
	{
		Entity owner;
		TimeOfDayComponent* timeOfDay = FindTimeOfDay(world, &owner);
		if (!timeOfDay)
		{
			bRunning = false;
			return;
		}

		if (timeOfDay->bTimeRuns)
		{
			const float dayLength = std::max(timeOfDay->DayLengthSeconds, 1.0f);
			if (!bRunning || owner != Owner || timeOfDay->Hours != WrittenHours || dayLength != DayLengthSeconds)
			{
				bRunning = true;
				Owner = owner;
				FromHours = WrapHours(timeOfDay->Hours);
				FromSeconds = step.WorldTimeSeconds;
				DayLengthSeconds = dayLength;
			}
			// The time at the step's end
			const double seconds = step.WorldTimeSeconds + step.FixedDeltaSeconds - FromSeconds;
			timeOfDay->Hours = WrapHours(FromHours + seconds * HoursPerDay / DayLengthSeconds);
			WrittenHours = timeOfDay->Hours;
		}
		else
		{
			bRunning = false;
		}
		ApplyTimeOfDay(world);
	}
}

#include "NyxPCH.h"
#include "DrawnWorldTime.h"

#include "FixedStepClock.h"

namespace Nyx
{
	float GetDrawnWorldTime(const FixedStepClock& clock)
	{
		// The world's time is the last step's end; drawn between that step's start and end, the
		// moment drawn is as far back from it as the poses are
		return static_cast<float>(clock.GetWorldTimeSeconds() - (1.0 - clock.GetDrawFactor()) * clock.GetStepSeconds());
	}
}

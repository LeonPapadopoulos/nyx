#include "NyxPCH.h"
#include "GameWorld.h"

#include "Assertions.h"
#include "Profiler.h"

#include <algorithm>

namespace Nyx
{
	ISimSystem& GameWorld::AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep)
	{
		ASSERT(system && "A GameWorld needs a system to add.");
		Systems.push_back(SystemEntry{ std::move(system), std::max(everyNthStep, 1u) });
		return *Systems.back().System;
	}

	void GameWorld::Step(const StepTime& step)
	{
		NYX_PROFILE_SCOPE("GameWorld::Step");
		const SimContext context{ World, step };
		for (const SystemEntry& entry : Systems)
		{
			if (step.StepIndex % entry.EveryNthStep == 0)
			{
				// One zone per system, named after it, so the one over the step's budget shows
				NYX_PROFILE_SCOPE("System");
				NYX_PROFILE_ZONE_NAME(entry.System->GetName());
				entry.System->Step(context);
			}
		}
	}
}

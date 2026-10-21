#include "NyxPCH.h"
#include "GameWorld.h"

#include "Assertions.h"

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
		const SimContext context{ World, step };
		for (const SystemEntry& entry : Systems)
		{
			if (step.StepIndex % entry.EveryNthStep == 0)
			{
				entry.System->Step(context);
			}
		}
	}
}

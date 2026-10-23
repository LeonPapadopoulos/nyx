#include "NyxPCH.h"
#include "GameWorld.h"

#include "Assertions.h"
#include "Profiler.h"

#include <algorithm>

namespace Nyx
{
	ISimSystem& GameWorld::AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep, ESimStage stage)
	{
		ASSERT(system && "A GameWorld needs a system to add.");
		Systems.push_back(SystemEntry{ std::move(system), std::max(everyNthStep, 1u), stage });
		return *Systems.back().System;
	}

	void GameWorld::Step(const StepTime& step)
	{
		NYX_PROFILE_SCOPE("GameWorld::Step");
		const float seconds = static_cast<float>(step.FixedDeltaSeconds);
		Debug.Advance(seconds);

		Bodies.SyncBeforeStep(World, Physics, seconds);
		RefreshEntityGuidLookup(World, Guids);

		const SimContext context{ World, step, Debug, Physics, Bodies, Guids };
		RunStage(ESimStage::BeforePhysics, context);

		Physics.Step(seconds);
		Bodies.WriteBackAfterStep(World, Physics);
		if (PhysicsDebugView)
		{
			Physics.DrawDebug(Debug, *PhysicsDebugView);
		}

		RunStage(ESimStage::AfterPhysics, context);
	}

	void GameWorld::SetPhysicsDebugView(const std::optional<PhysicsDebugOptions>& options)
	{
		PhysicsDebugView = options;
		// Contact points cost a little each step, so they are kept only to be drawn
		Physics.SetKeepContactPoints(options && options->bContacts);
	}

	void GameWorld::RunStage(ESimStage stage, const SimContext& context)
	{
		for (const SystemEntry& entry : Systems)
		{
			if (entry.Stage == stage && context.Step.StepIndex % entry.EveryNthStep == 0)
			{
				// One zone per system, named after it, so the one over the step's budget shows
				NYX_PROFILE_SCOPE("System");
				NYX_PROFILE_ZONE_NAME(entry.System->GetName());
				entry.System->Step(context);
			}
		}
	}
}

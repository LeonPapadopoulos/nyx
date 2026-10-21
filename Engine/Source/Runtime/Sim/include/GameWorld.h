#pragma once

#include "Entity.h"
#include "FixedStepClock.h"
#include "SimSystem.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	// The world a game runs: its entities, and the systems that change them each fixed step, in
	// one order kept here rather than spread over registrations. The game's GameLayer owns one; a
	// game module adds its systems (GameModule::CreateSystems).
	class GameWorld
	{
	public:
		GameWorld() = default;
		GameWorld(const GameWorld&) = delete;
		GameWorld& operator=(const GameWorld&) = delete;

		Registry& GetRegistry() { return World; }
		const Registry& GetRegistry() const { return World; }

		// Adds the system after those added before. It runs on every `everyNthStep`th step (1:
		// every step), counted from step 0, so systems with the same interval run in the same steps.
		ISimSystem& AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep = 1);

		// Runs the systems due in this step, in the order they were added
		void Step(const StepTime& step);

		struct SystemEntry
		{
			std::unique_ptr<ISimSystem> System;
			uint32_t EveryNthStep = 1;
		};

		const std::vector<SystemEntry>& GetSystems() const { return Systems; }

	private:
		Registry World;
		std::vector<SystemEntry> Systems;
	};
}

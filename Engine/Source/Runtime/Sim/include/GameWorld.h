#pragma once

#include "DebugDraw.h"
#include "Entity.h"
#include "FixedStepClock.h"
#include "PhysicsBodies.h"
#include "PhysicsWorld.h"
#include "SimSystem.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	// The world a game runs: its entities, their physics, and the systems that change them each
	// fixed step, in one order kept here rather than spread over registrations. The game's GameLayer
	// owns one; a game module adds its systems (GameModule::CreateSystems). A step:
	// 1. the debug drawing moves on: what the last step drew goes, timed shapes age;
	// 2. bodies follow the physics components and the transforms set since the last step
	//    (PhysicsBodies);
	// 3. the systems of ESimStage::BeforePhysics, in the order they were added;
	// 4. the physics step;
	// 5. dynamic bodies write their poses into their transforms, and where they were before into
	//    their PreviousTransformComponents;
	// 6. the systems of ESimStage::AfterPhysics, in the order they were added.
	class GameWorld
	{
	public:
		GameWorld() = default;
		GameWorld(const GameWorld&) = delete;
		GameWorld& operator=(const GameWorld&) = delete;

		Registry& GetRegistry() { return World; }
		const Registry& GetRegistry() const { return World; }

		// What the systems draw over the world; the game hands it to the renderer
		// (IRenderer::SetWorldDebugDraw())
		DebugDraw& GetDebugDraw() { return Debug; }
		const DebugDraw& GetDebugDraw() const { return Debug; }

		PhysicsWorld& GetPhysics() { return Physics; }
		const PhysicsWorld& GetPhysics() const { return Physics; }
		const PhysicsBodies& GetBodies() const { return Bodies; }

		// Adds the system after those of its stage added before. It runs on every `everyNthStep`th
		// step (1: every step), counted from step 0, so systems with the same interval run in the
		// same steps.
		ISimSystem& AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep = 1,
			ESimStage stage = ESimStage::BeforePhysics);

		// One step of the world, in the order above
		void Step(const StepTime& step);

		struct SystemEntry
		{
			std::unique_ptr<ISimSystem> System;
			uint32_t EveryNthStep = 1;
			ESimStage Stage = ESimStage::BeforePhysics;
		};

		const std::vector<SystemEntry>& GetSystems() const { return Systems; }

	private:
		// The systems of one stage due in this step
		void RunStage(ESimStage stage, const SimContext& context);

		Registry World;
		DebugDraw Debug;
		PhysicsWorld Physics;
		PhysicsBodies Bodies;
		std::vector<SystemEntry> Systems;
	};
}

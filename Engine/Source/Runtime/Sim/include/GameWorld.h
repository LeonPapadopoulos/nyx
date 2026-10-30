#pragma once

#include "CommandQueue.h"
#include "DebugDraw.h"
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "FixedStepClock.h"
#include "PhysicsBodies.h"
#include "PhysicsConstraints.h"
#include "PhysicsMaterials.h"
#include "PhysicsWorld.h"
#include "SimSystem.h"
#include "TerrainCollision.h"
#include "TimeOfDayClock.h"
#include "Wind.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Nyx
{
	class IWindField;

	// The world a game runs: its entities, their physics, and the systems that change them each
	// fixed step, in one order kept here rather than spread over registrations. The game's GameLayer
	// owns one; a game module adds its systems (GameModule::CreateSystems). A step:
	// 0. the commands players added since the last step become this step's (SimContext::Commands);
	// 1. the debug drawing moves on: what the last step drew goes, timed shapes age;
	// 2. bodies and characters follow the physics components, their physics materials and the
	//    transforms set since the last step (PhysicsBodies); the SDF terrains' chunks round the
	//    moving bodies get their bodies (TerrainCollision);
	// 3. the lookup from guid to entity is made again, in the memory it had (SimContext::Guids);
	//    constraints follow their components and the bodies (PhysicsConstraints); the players'
	//    MoveCharacterCommands go into their characters (ApplyMoveCharacterCommands()); the time of
	//    day moves on to the step's end while it runs, and the sun turns to it (TimeOfDayClock);
	//    the wind turns towards its WindComponent's values, and is as it is at the step's start
	//    (UpdateWorldWind(), SimContext::Wind);
	// 4. the systems of ESimStage::BeforePhysics, in the order they were added, e.g. to change
	//    where the characters go (CharacterComponent::DesiredMove);
	// 5. the characters walk, and write their transforms (PhysicsBodies::MoveCharacters());
	// 6. the physics step;
	// 7. dynamic bodies write their poses into their transforms, and where they were before into
	//    their PreviousTransformComponents, as do kinematic bodies the systems moved; constraints
	//    write what they held into their ConstraintStateComponents, and those that broke switch off;
	//    the physics debug view is drawn, if on;
	// 8. the systems of ESimStage::AfterPhysics, in the order they were added.
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
		const TerrainCollision& GetTerrainCollision() const { return TerrainColliders; }
		const PhysicsConstraints& GetConstraints() const { return Constraints; }
		// The physics materials the colliders name, read once and kept; a game's tools hand it the
		// editor's values (PhysicsMaterials::SetText())
		PhysicsMaterials& GetPhysicsMaterials() { return Materials; }
		// The physics material of the entity's collider, the engine's without one (GetExtra() reads a
		// game's own numbers from it)
		const PhysicsMaterialData& GetPhysicsMaterial(Entity entity) { return Nyx::GetPhysicsMaterial(World, Materials, entity); }

		// Draws the physics world into the debug drawing after each physics step, as the options say
		// (PhysicsWorld::DrawDebug()), from the next step on; none, the default, draws nothing
		void SetPhysicsDebugView(const std::optional<PhysicsDebugOptions>& options);
		const std::optional<PhysicsDebugOptions>& GetPhysicsDebugView() const { return PhysicsDebugView; }

		// The air the game keeps on top of the world's wind (its own winds, the draught of a fire),
		// which the game hands to the renderer once the systems are made (IRenderer::SetWorldWindField())
		// for the particles and the grass and trees; none, the default: only the world's wind. A game
		// module sets it in CreateSystems(), and it must live as long as the world's systems.
		void SetWindField(const IWindField* field) { WindField = field; }
		const IWindField* GetWindField() const { return WindField; }

		// The world's wind as the last step began (UpdateWorldWind()), which the systems get too
		// (SimContext::Wind); calm before the first step
		const WindState& GetWind() const { return Wind; }

		// Adds the system after those of its stage added before. It runs on every `everyNthStep`th
		// step (1: every step), counted from step 0, so systems with the same interval run in the
		// same steps.
		ISimSystem& AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep = 1,
			ESimStage stage = ESimStage::BeforePhysics);

		// One step of the world, in the order above
		void Step(const StepTime& step);

		// Where players' commands go between steps (IPlayerController): the next step hands them to
		// its systems, then they are gone. Only those systems change the simulated world.
		CommandQueue& GetCommands() { return Commands; }

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
		TerrainCollision TerrainColliders;
		PhysicsConstraints Constraints;
		PhysicsMaterials Materials;
		TimeOfDayClock TimeOfDay;
		EntityGuidLookup Guids;
		// Added since the last step, and the ones the step running now applies
		CommandQueue Commands;
		CommandQueue StepCommands;
		std::optional<PhysicsDebugOptions> PhysicsDebugView;
		const IWindField* WindField = nullptr;
		WindState Wind;
		std::vector<SystemEntry> Systems;
	};
}

#include "NyxPCH.h"
#include "GameWorld.h"

#include "Assertions.h"
#include "CharacterCommands.h"
#include "HierarchyCommands.h"
#include "PrefabCommands.h"
#include "Profiler.h"
#include "TerrainEditCommands.h"
#include "WindComponent.h"
#include "WorldSave.h"

#include <algorithm>

namespace Nyx
{
	ISimSystem& GameWorld::AddSystem(std::unique_ptr<ISimSystem> system, uint32_t everyNthStep, ESimStage stage)
	{
		ASSERT(system && "A GameWorld needs a system to add.");
		Systems.push_back(SystemEntry{ std::move(system), std::max(everyNthStep, 1u), stage });
		return *Systems.back().System;
	}

	bool GameWorld::TakeSaveSnapshot(WorldSnapshot& outSnapshot) const
	{
		if (!TakeWorldSnapshot(World, outSnapshot, Assets))
		{
			return false;
		}
		Bodies.GetMotions(World, Physics, outSnapshot.Motions);
		return true;
	}

	bool GameWorld::SaveToFile(const WorldSaveBase& base, const WorldSaveClock& clock, const std::filesystem::path& path) const
	{
		WorldSnapshot snapshot;
		BinaryWriter out;
		return TakeSaveSnapshot(snapshot) && WriteWorldSave(base, snapshot, clock, out) && out.SaveToFile(path);
	}

	bool GameWorld::LoadSave(const std::filesystem::path& path, const ScenePostLoadContext& postLoadContext, WorldSaveBase& outBase, WorldSaveClock& outClock,
		std::string& outError)
	{
		std::vector<WorldSaveMotion> motions;
		if (!LoadWorldSave(path, World, postLoadContext, outBase, outClock, outError, &motions))
		{
			return false;
		}
		Bodies.SetStartMotions(World, motions);
		return true;
	}

	void GameWorld::Step(const StepTime& step)
	{
		NYX_PROFILE_SCOPE("GameWorld::Step");
		StepCommands.Clear();
		StepCommands.TakeFrom(Commands);

		const float seconds = static_cast<float>(step.FixedDeltaSeconds);
		Debug.Advance(seconds);

		Bodies.SyncBeforeStep(World, Physics, Materials, seconds);
		TerrainColliders.SyncBeforeStep(World, Physics, Bodies);
		RefreshEntityGuidLookup(World, Guids);
		Constraints.SyncBeforeStep(World, Physics, Bodies, Guids);
		ApplySpawnPrefabCommands(World, Guids, StepCommands, Assets, &Spawned);
		// What was spawned, in the lookup at once, so the step's other commands and systems find it
		if (!Spawned.empty())
		{
			RefreshEntityGuidLookup(World, Guids);
		}
		ApplyHierarchyCommands(World, Guids, StepCommands);
		ApplyMoveCharacterCommands(World, Guids, StepCommands);
		ApplyTerrainEditCommands(World, Guids, StepCommands);
		TimeOfDay.Step(World, step);
		Wind = UpdateWorldWind(World, step.WorldTimeSeconds);

		const SimContext context{ World, step, Debug, Physics, Bodies, Guids, StepCommands, Constraints, Materials, Wind };
		RunStage(ESimStage::BeforePhysics, context);

		// Where the systems said they want to go, before the bodies move: they meet the characters
		// where these are now
		Bodies.MoveCharacters(World, Physics, seconds);
		Physics.Step(seconds);
		Bodies.WriteBackAfterStep(World, Physics);
		Constraints.ReadBackAfterStep(World, Physics);
		Followers.Update(World, Guids);
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

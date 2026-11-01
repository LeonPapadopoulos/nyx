#pragma once

#include "Entity.h"

#include <vector>

namespace Nyx
{
	class CommandQueue;
	class IAssetResolver;
	struct EntityGuidLookup;

	// The step's SpawnPrefabCommands, in the order they came: each makes an instance of its prefab
	// (ApplyPrefab()), from the prefabs and with the assets `assets` resolves, the post-load steps of
	// what it adds run. Dropped, with a warning: a command without a guid, with a guid the world has
	// (`guids`, made before) or an earlier command of the step took, or whose prefab isn't there.
	// GameWorld::Step() calls it before its systems; the instances' bodies are made the next step,
	// and they are in the guid lookup from then on. outSpawned, if given, gets the roots made.
	void ApplySpawnPrefabCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands, IAssetResolver* assets,
		std::vector<Entity>* outSpawned = nullptr);
}

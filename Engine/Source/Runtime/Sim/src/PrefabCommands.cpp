#include "NyxPCH.h"
#include "PrefabCommands.h"

#include "CommandQueue.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "Prefab.h"
#include "SpawnPrefabCommand.h"
#include "TransformComponent.h"

#include <unordered_set>

namespace Nyx
{
	void ApplySpawnPrefabCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands, IAssetResolver* assets,
		std::vector<Entity>* outSpawned)
	{
		if (outSpawned)
		{
			outSpawned->clear();
		}

		std::unordered_set<EntityGuid> spawned;
		commands.ForEach<SpawnPrefabCommand>(
			[&](const SpawnPrefabCommand& command)
			{
				if (!command.Guid.IsValid() || FindEntityByGuid(world, guids, command.Guid) || !spawned.insert(command.Guid).second)
				{
					LOG_WARNING("Spawn: dropped an instance of '{0}', as its guid {1} is none or taken", command.Prefab.Path, GuidToText(command.Guid));
					return;
				}
				const PrefabAsset* prefab = assets && command.Prefab.IsValid() ? assets->ResolvePrefab(command.Prefab.Path) : nullptr;
				if (!prefab)
				{
					LOG_WARNING("Spawn: dropped an instance of '{0}', as the prefab isn't there", command.Prefab.Path);
					return;
				}

				const Entity root = world.CreateEntity();
				world.Add<GuidComponent>(root, GuidComponent{ command.Guid });
				TransformComponent transform;
				transform.Position = command.Position;
				transform.Rotation = glm::normalize(command.Rotation);
				transform.Scale = command.Scale;
				world.Add<TransformComponent>(root, transform);
				world.Add<PrefabInstanceComponent>(root, PrefabInstanceComponent{ .Prefab = command.Prefab });

				ScenePostLoadContext context{ .AssetResolver = assets };
				ApplyPrefab(world, root, *prefab, context);
				if (outSpawned)
				{
					outSpawned->push_back(root);
				}
			});
	}
}

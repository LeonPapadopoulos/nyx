#include "NyxPCH.h"
#include "HierarchyCommands.h"

#include "CommandQueue.h"
#include "EntityCommands.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "Log.h"
#include "ParentComponent.h"
#include "PreviousTransformComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace Nyx
{
	namespace
	{
		std::optional<Entity> Find(const Registry& world, const EntityGuidLookup& guids, EntityGuid guid)
		{
			return guid.IsValid() ? FindEntityByGuid(world, guids, guid) : std::nullopt;
		}

		// Whether `entity` is `ancestor` or below it, up the links the world has; a circle ends the walk
		bool IsSelfOrBelow(const Registry& world, const EntityGuidLookup& guids, Entity entity, Entity ancestor)
		{
			std::vector<Entity> met;
			std::optional<Entity> current = entity;
			while (current && std::find(met.begin(), met.end(), *current) == met.end())
			{
				if (*current == ancestor)
				{
					return true;
				}
				met.push_back(*current);
				current = world.Has<ParentComponent>(*current) ? Find(world, guids, world.Get<ParentComponent>(*current).Parent) : std::nullopt;
			}
			return false;
		}
	}

	void ApplyHierarchyCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands)
	{
		commands.ForEach<DestroyEntityCommand>(
			[&](const DestroyEntityCommand& command)
			{
				const std::optional<Entity> entity = Find(world, guids, command.Entity);
				if (!entity)
				{
					LOG_WARNING("Destroy: dropped, as the world hasn't got entity {0}", GuidToText(command.Entity));
					return;
				}
				for (const Entity below : GetTransformSubtree(world, *entity))
				{
					world.DestroyEntity(below);
				}
			});

		commands.ForEach<DetachEntityCommand>(
			[&](const DetachEntityCommand& command)
			{
				const std::optional<Entity> entity = Find(world, guids, command.Entity);
				if (!entity || !world.Has<ParentComponent>(*entity))
				{
					LOG_WARNING("Detach: dropped, as the world hasn't got entity {0} with a parent", GuidToText(command.Entity));
					return;
				}
				world.Remove<ParentComponent>(*entity);
				// Where it is now, not smeared from where its parent last took it
				world.Remove<PreviousTransformComponent>(*entity);
			});

		commands.ForEach<AttachEntityCommand>(
			[&](const AttachEntityCommand& command)
			{
				const std::optional<Entity> entity = Find(world, guids, command.Entity);
				const std::optional<Entity> parent = Find(world, guids, command.Parent);
				if (!entity || !parent || !world.Has<TransformComponent>(*entity) || !world.Has<TransformComponent>(*parent))
				{
					LOG_WARNING("Attach: dropped, as the world hasn't got entity {0} and parent {1}, both with a transform", GuidToText(command.Entity),
						GuidToText(command.Parent));
					return;
				}
				if (IsSelfOrBelow(world, guids, *parent, *entity))
				{
					LOG_WARNING("Attach: dropped, as {0} is entity {1} or below it", GuidToText(command.Parent), GuidToText(command.Entity));
					return;
				}

				ParentComponent link = world.Has<ParentComponent>(*entity) ? world.Get<ParentComponent>(*entity) : ParentComponent{};
				link.Parent = command.Parent;
				link.bFollowInGame = command.bFollow;
				SetLocalTransform(link, world.Get<TransformComponent>(*parent), world.Get<TransformComponent>(*entity));
				if (world.Has<ParentComponent>(*entity))
				{
					// Changed in place: what keeps track of the links looks at them again
					world.Get<ParentComponent>(*entity) = link;
					world.MarkChanged<ParentComponent>();
				}
				else
				{
					world.Add<ParentComponent>(*entity, link);
				}
			});
	}
}

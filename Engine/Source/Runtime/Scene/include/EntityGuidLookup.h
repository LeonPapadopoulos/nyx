#pragma once

#include "Entity.h"
#include "EntityGuid.h"

#include <optional>
#include <unordered_map>

namespace Nyx
{
	// The entities of a world by guid, made in one pass over its GuidComponents. FindEntityByGuid()
	// on the world goes through all of them on every call; make one of these when a frame looks up
	// many guids, such as resolving every surface anchor. It is a snapshot: make it again after
	// entities or their guids change, at the latest once a frame.
	struct EntityGuidLookup
	{
		std::unordered_map<EntityGuid, Entity> Entities;
	};

	// Where two entities share a guid, keeps the one FindEntityByGuid() on the world would find
	EntityGuidLookup MakeEntityGuidLookup(const Registry& world);

	// The entity that had the guid when the lookup was made, if the world still has it
	std::optional<Entity> FindEntityByGuid(const Registry& world, const EntityGuidLookup& lookup, EntityGuid guid);
}

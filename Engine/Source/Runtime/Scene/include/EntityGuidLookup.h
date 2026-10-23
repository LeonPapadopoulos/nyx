#pragma once

#include "Entity.h"
#include "EntityGuid.h"

#include <optional>
#include <vector>

namespace Nyx
{
	// The entities of a world by guid, made from its GuidComponents. FindEntityByGuid()
	// on the world goes through all of them on every call; make one of these when a frame looks up
	// many guids, such as resolving every surface anchor. It is a snapshot: make it again after
	// entities or their guids change, at the latest once a frame. A game world's systems get one,
	// made each step (SimContext::Guids).
	struct EntityGuidLookup
	{
		// One guid and its entity; an invalid guid marks a free slot
		struct Slot
		{
			EntityGuid Guid;
			Entity Owner;
		};

		// A hash table with linear probing, at most half full, so a lookup tries one or two slots.
		// Read it through FindEntityByGuid().
		std::vector<Slot> Slots;
	};

	// Where two entities share a guid, keeps the one FindEntityByGuid() on the world would find
	EntityGuidLookup MakeEntityGuidLookup(const Registry& world);

	// Makes the lookup again in the memory it has: allocates only when the world has more guids than
	// it ever had, so making it each step costs no allocations.
	void RefreshEntityGuidLookup(const Registry& world, EntityGuidLookup& lookup);

	// The entity that had the guid when the lookup was made, if the world still has it
	std::optional<Entity> FindEntityByGuid(const Registry& world, const EntityGuidLookup& lookup, EntityGuid guid);
}

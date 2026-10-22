#include "NyxPCH.h"
#include "EntityGuidLookup.h"

#include "GuidComponent.h"

namespace Nyx
{
	EntityGuidLookup MakeEntityGuidLookup(const Registry& world)
	{
		EntityGuidLookup lookup;

		// The same order as FindEntityByGuid(), and emplace keeps the first of a guid
		world.Each<GuidComponent>([&](Entity entity, const GuidComponent& component)
			{
				if (component.Guid.IsValid())
				{
					lookup.Entities.emplace(component.Guid, entity);
				}
			});

		return lookup;
	}

	std::optional<Entity> FindEntityByGuid(const Registry& world, const EntityGuidLookup& lookup, EntityGuid guid)
	{
		const auto it = lookup.Entities.find(guid);
		if (it == lookup.Entities.end() || !world.IsAlive(it->second))
		{
			return std::nullopt;
		}

		return it->second;
	}
}

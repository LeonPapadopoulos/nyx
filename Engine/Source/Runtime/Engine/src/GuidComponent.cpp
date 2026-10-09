#include "NyxPCH.h"
#include "GuidComponent.h"

namespace Nyx::Engine
{
	std::optional<Entity> FindEntityByGuid(const Registry& world, EntityGuid guid)
	{
		if (!guid.IsValid())
		{
			return std::nullopt;
		}

		std::optional<Entity> found;

		world.Each<GuidComponent>([&](Entity entity, const GuidComponent& component)
			{
				if (!found && component.Guid == guid)
				{
					found = entity;
				}
			});

		return found;
	}
}

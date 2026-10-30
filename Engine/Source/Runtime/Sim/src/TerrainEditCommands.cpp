#include "NyxPCH.h"
#include "TerrainEditCommands.h"

#include "CommandQueue.h"
#include "EntityGuidLookup.h"
#include "SdfTerrain.h"
#include "SdfTerrainEditCommand.h"

namespace Nyx
{
	void ApplyTerrainEditCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands)
	{
		commands.ForEach<SdfTerrainEditCommand>(
			[&](const SdfTerrainEditCommand& command)
			{
				const std::optional<Entity> entity = FindEntityByGuid(world, guids, command.Terrain);
				if (!entity || !world.Has<SdfTerrainComponent>(*entity))
				{
					return;
				}
				AppendTerrainEdit(world.Get<SdfTerrainComponent>(*entity), command.Edit);
			});
	}
}

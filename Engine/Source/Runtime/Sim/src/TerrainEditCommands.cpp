#include "NyxPCH.h"
#include "TerrainEditCommands.h"

#include "CommandQueue.h"
#include "EntityGuidLookup.h"
#include "SdfTerrain.h"
#include "SdfTerrainEditCommand.h"

#include <algorithm>
#include <vector>

namespace Nyx
{
	void ApplyTerrainEditCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands)
	{
		std::vector<Entity> edited;
		commands.ForEach<SdfTerrainEditCommand>(
			[&](const SdfTerrainEditCommand& command)
			{
				const std::optional<Entity> entity = FindEntityByGuid(world, guids, command.Terrain);
				if (!entity || !world.Has<SdfTerrainComponent>(*entity))
				{
					return;
				}
				SdfTerrainEdit edit = command.Edit;
				edit.Kind = ESdfTerrainEditKind::Shape;
				edit.Baked.reset();
				AppendTerrainEdit(world.Get<SdfTerrainComponent>(*entity), std::move(edit));
				if (std::find(edited.begin(), edited.end(), *entity) == edited.end())
				{
					edited.push_back(*entity);
				}
			});
		for (const Entity entity : edited)
		{
			if (!world.Has<TerrainBakeCountsComponent>(entity))
			{
				world.Add<TerrainBakeCountsComponent>(entity, TerrainBakeCountsComponent{});
			}
			BakeTerrainBricks(world.Get<SdfTerrainComponent>(entity), world.Get<TerrainBakeCountsComponent>(entity).Counts);
		}
	}
}

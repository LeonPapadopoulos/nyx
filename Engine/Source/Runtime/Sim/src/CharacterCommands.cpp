#include "NyxPCH.h"
#include "CharacterCommands.h"

#include "CharacterComponent.h"
#include "CommandQueue.h"
#include "EntityGuidLookup.h"
#include "MoveCharacterCommand.h"

namespace Nyx
{
	void ApplyMoveCharacterCommands(Registry& world, const EntityGuidLookup& guids, const CommandQueue& commands)
	{
		commands.ForEach<MoveCharacterCommand>(
			[&](const MoveCharacterCommand& command)
			{
				const std::optional<Entity> entity = FindEntityByGuid(world, guids, command.Character);
				if (!entity || !world.Has<CharacterComponent>(*entity))
				{
					return;
				}
				CharacterComponent& character = world.Get<CharacterComponent>(*entity);
				character.DesiredMove = command.Move;
				// Taken by this step's walk, which clears it
				character.bJumpRequested = character.bJumpRequested || command.bJump;
			});
	}
}

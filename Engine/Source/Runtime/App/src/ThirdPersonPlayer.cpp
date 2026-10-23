#include "NyxPCH.h"
#include "ThirdPersonPlayer.h"

#include "CharacterComponent.h"
#include "CommandQueue.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "OrbitCameraComponent.h"
#include "OrbitCameraInput.h"

namespace Nyx
{
	ThirdPersonPlayer::ThirdPersonPlayer(Input::InputSystem& input)
		: ThirdPerson(input, Input::ThirdPersonInput::Settings{})
	{
	}

	void ThirdPersonPlayer::Update(const PlayerFrame& frame)
	{
		const Registry& world = frame.World.GetRegistry();
		const std::optional<Entity> character = FindThirdPersonCharacter(world);
		ThirdPerson.SetActive(character.has_value(), "A character and an orbit camera, and no player of the game module's");

		// Free look while exploring, the cursor free to point with otherwise: the orbit camera's
		// request, input state rather than the world's. Found by name, as the orbit camera's input
		// may come after this.
		Input::InputUser& user = frame.Input.GetUser();
		if (character && user.ConsumeTriggered(ThirdPerson.GetActions().ToggleFreeLook))
		{
			bFreeLook = !bFreeLook;
		}
		const Input::ContextHandle freeLook = frame.Input.FindContext(Input::OrbitCameraInput::Settings{}.FreeLookContextName);
		if (freeLook.IsValid())
		{
			const bool bAsk = character.has_value() && bFreeLook;
			user.SetContextActive(freeLook, bAsk, { .Reason = bAsk ? "Exploring (Tab: point with the cursor)" : "" });
		}

		if (character)
		{
			if (const std::optional<MoveCharacterCommand> command = MakeMoveCharacterCommand(frame, *character, ThirdPerson.GetActions()))
			{
				frame.Commands.Add(*command);
			}
		}
	}

	std::optional<Entity> FindThirdPersonCharacter(const Registry& world)
	{
		std::optional<EntityGuid> followed;
		world.Each<OrbitCameraComponent>(
			[&](Entity, const OrbitCameraComponent& orbit)
			{
				followed = followed ? followed : std::optional(orbit.Target);
			});
		if (!followed)
		{
			return std::nullopt;
		}

		const std::optional<Entity> target = FindEntityByGuid(world, *followed);
		if (target && world.Has<CharacterComponent>(*target))
		{
			return target;
		}
		std::optional<Entity> first;
		world.Each<CharacterComponent>(
			[&](Entity entity, const CharacterComponent&)
			{
				first = first ? first : std::optional(entity);
			});
		return first;
	}

	std::optional<MoveCharacterCommand> MakeMoveCharacterCommand(
		const PlayerFrame& frame, Entity character, const Input::ThirdPersonInput::Actions& actions)
	{
		return Input::MakeMoveCharacterCommand(frame.World.GetRegistry(), character, frame.Input.GetUser(), actions);
	}
}

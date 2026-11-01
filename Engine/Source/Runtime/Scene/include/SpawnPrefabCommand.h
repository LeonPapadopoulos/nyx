#pragma once

#include "AssetReference.h"
#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Nyx
{
	// A player's ask to place an instance of a prefab (Prefab.h), as a command (CommandQueue): the
	// game world makes it at the start of the next step, before its systems
	// (ApplySpawnPrefabCommands()), its root with this guid and transform, its parts with guids made
	// from it (GetPrefabPartGuid()). Whoever asks picks the guid (EntityGuid::Generate()), so its later
	// commands can name the instance before it is there. A command whose guid the world has already,
	// or whose prefab isn't there, is dropped.
	NYX_REFLECT()
	struct SpawnPrefabCommand
	{
		// Type "Prefab", the path in an asset folder ("Prefabs/Wall.nyxprefab")
		NYX_PROPERTY(Serialize)
		AssetReference Prefab;

		NYX_PROPERTY(Serialize)
		EntityGuid Guid;

		// The root's place in the world
		NYX_PROPERTY(Serialize)
		glm::vec3 Position{ 0.0f };

		NYX_PROPERTY(Serialize)
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Serialize)
		glm::vec3 Scale{ 1.0f };
	};
}

#include "Generated/Runtime/SpawnPrefabCommand.reflect.h"

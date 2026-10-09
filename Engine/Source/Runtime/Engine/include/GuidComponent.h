#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <optional>

namespace Nyx::Engine
{
	// Gives an entity its EntityGuid. Every entity of a scene has one: the editor adds it to the
	// entities it creates, and loading a scene adds it where it is missing.
	NYX_REFLECT(Component, meta = (DisplayName = "Guid Component"))
	struct GuidComponent
	{
		NYX_PROPERTY(Serialize, ReadOnly, meta = (Tooltip = "Names this entity the same way in its scene file and in every program that loads it"))
		EntityGuid Guid;
	};

	// The entity with the guid, if the world has one (never for an invalid guid). Goes through every
	// GuidComponent, which is fast enough for now; a map from guid to entity can follow once
	// something looks up many guids per frame.
	std::optional<Entity> FindEntityByGuid(const Registry& world, EntityGuid guid);
}

#include "Generated/Runtime/GuidComponent.reflect.h"

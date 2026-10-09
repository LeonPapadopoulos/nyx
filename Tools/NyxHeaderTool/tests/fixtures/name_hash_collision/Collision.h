#pragma once

#include "ReflectionMacros.h"

// The header tool must reject this type: "Uqyihla" and "Tzrrbfw" have the same FNV-1a
// name hash (0x53218BB0), so saved data couldn't tell the two properties apart.
// This fixture has no Input.h, so RegenerateGoldenFiles.bat skips it.
namespace Nyx::Engine
{
	NYX_REFLECT(Component)
	struct NameHashCollisionComponent
	{
		NYX_PROPERTY(Serialize)
		float Uqyihla = 0.0f;

		NYX_PROPERTY(Serialize)
		float Tzrrbfw = 0.0f;
	};
}

#include "Generated/Runtime/Collision.reflect.h"

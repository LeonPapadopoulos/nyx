#pragma once

#include "ReflectionMacros.h"

#include <cstdint>

// A run-time component of the test game module: what its Lantern Glow system made of a lantern as
// it runs, never saved (its REGISTER function makes it a run-time type), as a game's systems keep
// state of their own that the editor shows while it plays
namespace NyxTestModule
{
	NYX_REFLECT(Component, meta = (DisplayName = "Test Lantern Glow"))
	struct TestLanternGlowComponent
	{
		NYX_PROPERTY(Edit, Serialize)
		float Glow = 0.0f;

		NYX_PROPERTY(Edit, Serialize)
		int32_t Steps = 0;
	};
}

#include "Generated/NyxTestModule/TestLanternGlowComponent.reflect.h"

#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <string>

// Commands of the test game module: what its player asks of the simulation (CommandQueue), as a
// game declares its own
namespace NyxTestModule
{
	// Makes one lantern brighter
	NYX_REFLECT()
	struct TestBrightenCommand
	{
		NYX_PROPERTY(Serialize)
		Nyx::EntityGuid Lantern;

		NYX_PROPERTY(Serialize)
		float Amount = 0.0f;

		// Not a property, so it stays behind, as an Entity handle or a pointer would
		float LeftBehind = 0.0f;
	};

	// Labels every lantern
	NYX_REFLECT()
	struct TestRelabelCommand
	{
		NYX_PROPERTY(Serialize)
		std::string Label;
	};
}

#include "Generated/NyxTestModule/TestCommands.reflect.h"

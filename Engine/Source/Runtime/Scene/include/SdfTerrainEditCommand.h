#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"
#include "SdfTerrainComponent.h"

namespace Nyx
{
	// A player's ask to edit an SDF terrain, as a command (CommandQueue): the game world applies it at
	// the start of the next step (ApplyTerrainEditCommands()), after the terrain's other edits with the
	// next revision (AppendTerrainEdit()); the terrain's collision and drawn chunks follow, going live
	// together (TerrainEditActivationComponent). The edit's own Revision is ignored. A command whose
	// guid names no SDF terrain is dropped.
	NYX_REFLECT()
	struct SdfTerrainEditCommand
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Terrain;

		// In the terrain's space
		NYX_PROPERTY(Serialize)
		SdfTerrainEdit Edit;
	};
}

#include "Generated/Runtime/SdfTerrainEditCommand.reflect.h"

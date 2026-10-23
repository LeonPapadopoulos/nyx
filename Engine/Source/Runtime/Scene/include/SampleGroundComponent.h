#pragma once

#include "ReflectionMacros.h"

#include <cstdint>

namespace Nyx
{
	// A stand-in ground to try meshes made at run time on, until a ground generator: a square of
	// gentle bumps from a few sines, around the entity. SampleGroundSystem (Sim) gives the entity its
	// mesh (RuntimeMeshComponent) while the game runs, and again when these values change; a Mesh
	// Renderer shows it, a Mesh Collider makes it ground to stand on.
	NYX_REFLECT(Component, meta = (DisplayName = "Sample Ground"))
	struct SampleGroundComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sample Ground", DragSpeed = 0.1, Tooltip = "Its side, in metres"))
		float Size = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sample Ground", Tooltip = "Squares along each side, two triangles each; 1 to 512"))
		uint32_t Cells = 48;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sample Ground", DragSpeed = 0.01, Tooltip = "From the middle to the highest bump, in metres"))
		float BumpHeight = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sample Ground", DragSpeed = 0.1, Tooltip = "From one bump to the next, in metres"))
		float BumpSpacing = 8.0f;
	};
}

#include "Generated/Runtime/SampleGroundComponent.reflect.h"

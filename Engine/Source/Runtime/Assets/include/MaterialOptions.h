#pragma once

#include "ReflectionMacros.h"

namespace Nyx
{
	// How a material is drawn, besides its shader's values. Each property is a line of the material
	// file named as the property is, a bool's without its b ("TwoSided = true"); an instance takes
	// the options it doesn't set from its parents, and what no file sets is as here
	// (ReadMaterialOptions()).
	NYX_REFLECT(meta = (DisplayName = "Material Options"))
	struct MaterialOptions
	{
		// Single sheets, such as leaves, blades of grass and petals, seen from both sides: both sides
		// of each triangle are drawn, the back one with the normal turned round, so the light is
		// right from either side. Otherwise only the front is drawn, counter-clockwise as the camera
		// sees it.
		NYX_PROPERTY(Edit, Serialize, meta = (DisplayName = "Two-Sided", Tooltip = "Draws both sides of every triangle, the back one lit as a front, for single sheets such as leaves, blades and petals"))
		bool bTwoSided = false;

		// For a shader with a vertex function (ISurfaceVertex in nyx/surface.slang): how far it
		// moves a vertex at most, in metres in the world. The renderer widens every bounds it culls
		// a mesh of the material by this much, in the scene and in the shadows, so a swaying or
		// growing mesh isn't left out while a part of it is in view. 0: it moves nothing.
		NYX_PROPERTY(Edit, Serialize, meta = (DisplayName = "Max Vertex Offset", DragSpeed = 0.01, Tooltip = "How far the shader's vertex function moves a vertex at most, in metres: what is culled is judged by bounds this much larger"))
		float MaxVertexOffset = 0.0f;
	};
}

#include "Generated/Runtime/MaterialOptions.reflect.h"

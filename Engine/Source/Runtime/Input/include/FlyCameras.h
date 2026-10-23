#pragma once

#include "FlyCameraInput.h"

namespace Nyx
{
	class Registry;
}

namespace Nyx::Input
{
	// The game's fly cameras: moves and turns every entity with a FlyCameraComponent and a
	// TransformComponent by the fly camera's input this frame. The input's fly context is on only
	// while the world has such an entity, its mouse look context while one of them looks around (the
	// right mouse button held after a click). Without input it leaves the entities alone, so the
	// editor's live edits still move them. Once per frame.
	void UpdateFlyCameras(Registry& world, FlyCameraInput& input, float deltaTime);
}

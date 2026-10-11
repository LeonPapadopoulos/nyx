#pragma once

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	// The scene the editor starts with: three cubes with different materials, a camera looking at
	// them, and a sun.
	void SpawnDefaultScene(Nyx::SceneDocument& scene);
}

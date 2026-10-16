#pragma once

namespace Nyx::Editor
{
	class SceneDocument;

	// The scene the editor starts with: three cubes with different materials, a camera looking at
	// them, and a sun.
	void SpawnDefaultScene(SceneDocument& scene);
}

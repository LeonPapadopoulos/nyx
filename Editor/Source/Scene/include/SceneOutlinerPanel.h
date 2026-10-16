#pragma once

namespace Nyx::Editor
{
	class SceneEditor;

	// The "Scene Outliner" window: every entity by name, Add Entity, Delete and Duplicate, and a
	// right-click menu with Duplicate, Copy and Paste.
	class SceneOutlinerPanel
	{
	public:
		void Draw(SceneEditor& editing);

		bool bOpen = true;
	};
}

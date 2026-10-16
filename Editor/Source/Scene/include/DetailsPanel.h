#pragma once

namespace Nyx::Editor
{
	class SceneEditor;

	// The "Details" window: the selected entity's components with their reflected properties, Add
	// Component, and Remove Component on a component header's right-click menu.
	class DetailsPanel
	{
	public:
		void Draw(SceneEditor& editing);

		bool bOpen = true;
	};
}

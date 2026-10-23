#pragma once

#include "Entity.h"

#include <optional>

namespace Nyx::Editor
{
	class SceneEditor;

	// The "Scene Outliner" window: every entity by name, Add Entity, Delete and Duplicate, and a
	// right-click menu with Duplicate, Copy, Paste and Copy Guid. An entity dragged from it can be
	// dropped on a field that names one (EntityDragDrop.h).
	class SceneOutlinerPanel
	{
	public:
		void Draw(SceneEditor& editing);

		bool bOpen = true;

	private:
		// The entity a mouse press is on, selected once the button is up, unless it was dragged
		std::optional<Nyx::Entity> PressedEntity;
	};
}

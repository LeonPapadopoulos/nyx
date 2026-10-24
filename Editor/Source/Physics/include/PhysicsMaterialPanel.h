#pragma once

#include "InspectorDrawContext.h"

#include <string>

namespace Nyx::Editor
{
	class PhysicsMaterialDocument;
	class TransactionSystem;

	// The Physics Material window: the open physics material's values in the details table, its own
	// numbers of the game's among them, edited with the editor's undo; Save, and Ctrl+S while it has
	// the focus. Shown while a material is open; closing it, or opening another material, drops
	// values not saved.
	class PhysicsMaterialPanel
	{
	public:
		void Draw(PhysicsMaterialDocument& document, TransactionSystem& transactions);

		// Ctrl+S saves while the window has the focus; true when it did. Undo and redo are the
		// editor's, so they pass.
		bool HandleHotkeys(PhysicsMaterialDocument& document);

	private:
		void Save(PhysicsMaterialDocument& document);

		InspectorDrawContext DrawContext;
		bool bFocused = false;
		// Why the last save failed; empty once it worked
		std::string SaveError;
	};
}

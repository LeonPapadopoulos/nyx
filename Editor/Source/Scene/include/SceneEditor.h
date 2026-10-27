#pragma once

#include "ComponentPostLoadSubscriber.h"
#include "EditorTransactionContext.h"
#include "InspectorDrawContext.h"
#include "SceneDocument.h"
#include "SceneEntityTransactionDomain.h"
#include "SceneOutlineTransactionDomain.h"
#include "TransactionSystem.h"
#include "TransformGizmo.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Nyx::Editor
{
	// The scene being edited and how it is edited: undo, the details panel's and the gizmo's edits
	// in progress, and the entity clipboard. The panels draw it and the scene views pick in it;
	// this class has no UI of its own besides the editing hotkeys.
	class SceneEditor
	{
	public:
		SceneEditor();

		// The transaction context and the subscribers point at members.
		SceneEditor(const SceneEditor&) = delete;
		SceneEditor& operator=(const SceneEditor&) = delete;

		// Drops the details panel's and the gizmo's edits in progress without applying them. Needed
		// before anything that can move components in memory, since those edits point at components.
		void CancelPendingEdits();

		// Before the scene's registry is replaced (Open, New): undo steps, the selection and edits
		// in progress refer to entities of the old scene.
		void ForgetScene();

		// Add Entity and Delete in the outliner; both are undoable. A new entity goes right after the
		// selection in the outliner, in its folder; without a selection, at the end of `folder` (an
		// outliner folder's id, 0 for the top).
		void CreateEntity(uint64_t folder = 0);
		void DeleteSelectedEntity();

		// A pasted or duplicated entity gets a new guid and becomes the selection. It goes right
		// after the selection in the outliner, as a new entity does.
		void DuplicateSelectedEntity();
		void CopySelectedEntity();
		void PasteEntity();
		bool CanPaste() const
		{
			return !EntityClipboard.empty();
		}

		// Undo and redo can bring an entity back in another slot, so the selection follows its guid.
		void Undo();
		void Redo();

		// Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z; Ctrl+D, Ctrl+C and Ctrl+V. Not while a text field has focus.
		void HandleHotkeys();

	public:
		SceneDocument Scene;

		// Counts scene replacements. Widgets use it in their IDs, so a replacement scene, which can
		// reuse entity handles, gets no drag or text edit left over from the previous one.
		uint64_t SceneRevision = 0;

		SceneEntityTransactionDomain EntityDomain;
		// The outliner's folders and order (SceneOutlineEdits.h)
		SceneOutlineTransactionDomain OutlineDomain;
		TransactionSystem Transactions;
		EditorTransactionContext TransactionContext;

		// The details panel's edits in progress
		InspectorDrawContext DetailsContext;

		TransformGizmo Gizmo;

		// Loads the assets of entities after edits, undo and redo, e.g. the mesh of a typed mesh
		// path. Subscribed once the editor has an asset resolver.
		ComponentPostLoadSubscriber AssetLoader{ Scene };

	private:
		// Selects the entity a pasted copy or duplicate became, after dropping edits in progress.
		void PasteAndSelect(const std::vector<std::byte>& copied, const char* label);

		// The copied entity, as scene files store it (EntityCopies.h); the editor's own clipboard,
		// so it survives deleting the entity and opening another scene
		std::vector<std::byte> EntityClipboard;
	};
}

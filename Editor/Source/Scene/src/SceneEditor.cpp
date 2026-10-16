#include "SceneEditor.h"

#include "EntityCopies.h"
#include "RootObjectSnapshotUtils.h"
#include "TransactionObjectRefHelpers.h"

#include "imgui.h"

namespace Nyx::Editor
{
	SceneEditor::SceneEditor()
	{
		TransactionContext.ActiveScene = &Scene;
		Transactions.RegisterDomain(EObjectDomain::SceneEntity, &EntityDomain);
	}

	void SceneEditor::CancelPendingEdits()
	{
		DetailsContext.CancelPendingEdits();
		Gizmo.CancelInteraction();
	}

	void SceneEditor::ForgetScene()
	{
		// Cancel before destroying the components captured by these unfinished edits.
		CancelPendingEdits();

		// Undo steps and the selection refer to entities by handle. In the new scene, the same
		// handles name other entities, so undo would change the wrong ones.
		Scene.GetSelection().reset();
		Transactions.Clear();
		++SceneRevision;
	}

	void SceneEditor::CreateEntity()
	{
		const Nyx::Entity newEntity = Scene.CreateEntity("New Entity");
		Scene.GetSelection() = newEntity;

		const ObjectRef rootRef = MakeSceneEntityRef(Scene, newEntity);
		RootObjectSnapshot snapshot = CaptureRootObjectSnapshot(EntityDomain, TransactionContext, rootRef);

		Change change{};
		change.Kind = EChangeKind::AddObject;
		change.Payload = AddObjectChange{
			.Target = rootRef,
			.AfterCreate = snapshot
		};

		Transaction transaction{};
		transaction.Label = "Create Entity";
		transaction.Changes.push_back(std::move(change));

		Transactions.Push(std::move(transaction));
	}

	void SceneEditor::DeleteSelectedEntity()
	{
		std::optional<Nyx::Entity>& selection = Scene.GetSelection();
		if (!selection)
		{
			return;
		}

		const Nyx::Entity entityToDelete = *selection;
		const ObjectRef rootRef = MakeSceneEntityRef(Scene, entityToDelete);
		RootObjectSnapshot snapshot = CaptureRootObjectSnapshot(EntityDomain, TransactionContext, rootRef);

		if (!Scene.DestroyEntity(entityToDelete))
		{
			return;
		}

		Change change{};
		change.Kind = EChangeKind::DeleteObject;
		change.Payload = DeleteObjectChange{
			.Target = rootRef,
			.BeforeDelete = snapshot
		};

		Transaction transaction{};
		transaction.Label = "Delete Entity";
		transaction.Changes.push_back(std::move(change));

		Transactions.Push(std::move(transaction));
		selection.reset();
	}

	void SceneEditor::CopySelectedEntity()
	{
		const std::optional<Nyx::Entity>& selection = Scene.GetSelection();
		if (!selection)
		{
			return;
		}

		std::vector<std::byte> copied = CopyEntity(Scene, *selection);
		if (!copied.empty())
		{
			EntityClipboard = std::move(copied);
		}
	}

	void SceneEditor::DuplicateSelectedEntity()
	{
		const std::optional<Nyx::Entity>& selection = Scene.GetSelection();
		if (!selection)
		{
			return;
		}

		// Like copy and paste, but the clipboard keeps what it had
		PasteAndSelect(CopyEntity(Scene, *selection), "Duplicate Entity");
	}

	void SceneEditor::PasteEntity()
	{
		if (EntityClipboard.empty())
		{
			return;
		}

		PasteAndSelect(EntityClipboard, "Paste Entity");
	}

	void SceneEditor::PasteAndSelect(const std::vector<std::byte>& copied, const char* label)
	{
		// A new entity can move components in memory; edits in progress point at components
		CancelPendingEdits();

		if (const std::optional<Nyx::Entity> pasted =
				Nyx::Editor::PasteEntity(Scene, Transactions, EntityDomain, TransactionContext, copied, label))
		{
			Scene.GetSelection() = *pasted;
		}
	}

	void SceneEditor::Undo()
	{
		// The assets of what undo changed are loaded by AssetLoader
		std::optional<Nyx::Entity>& selection = Scene.GetSelection();
		const Nyx::EntityGuid selectedGuid = selection ? Scene.GetGuid(*selection) : Nyx::EntityGuid{};

		if (Transactions.Undo(TransactionContext) && selectedGuid.IsValid())
		{
			selection = Scene.FindEntity(selectedGuid);
		}
	}

	void SceneEditor::Redo()
	{
		std::optional<Nyx::Entity>& selection = Scene.GetSelection();
		const Nyx::EntityGuid selectedGuid = selection ? Scene.GetGuid(*selection) : Nyx::EntityGuid{};

		if (Transactions.Redo(TransactionContext) && selectedGuid.IsValid())
		{
			selection = Scene.FindEntity(selectedGuid);
		}
	}

	void SceneEditor::HandleHotkeys()
	{
		// Text fields use these keys for their text
		if (ImGui::GetIO().WantTextInput)
		{
			return;
		}

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
		{
			Undo();
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
			ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
		{
			Redo();
		}

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
		{
			DuplicateSelectedEntity();
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
		{
			CopySelectedEntity();
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V))
		{
			PasteEntity();
		}
	}
}

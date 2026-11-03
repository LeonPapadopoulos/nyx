#include "SceneEditor.h"

#include "EntityCopies.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneOutlineEdits.h"
#include "TransactionObjectRefHelpers.h"
#include "TransformComponent.h"
#include "TransformParents.h"

#include "imgui.h"

namespace
{
	using namespace Nyx::Editor;

	// A copy of each selected entity, but for those below another selected one, which go with it
	std::vector<std::vector<std::byte>> CopySelected(const SceneDocument& scene)
	{
		const Nyx::Registry& world = scene.GetRegistry();
		const std::vector<Nyx::Entity>& selected = scene.GetSelectedEntities();
		std::vector<std::vector<std::byte>> copies;
		for (const Nyx::Entity entity : selected)
		{
			const bool bGoesAlong = world.IsAlive(entity) && std::any_of(selected.begin(), selected.end(),
				[&](Nyx::Entity other)
				{
					return other != entity && world.IsAlive(other) && IsSelfOrBelow(world, entity, other);
				});
			if (bGoesAlong)
			{
				continue;
			}
			if (std::vector<std::byte> copied = CopyEntity(scene, entity); !copied.empty())
			{
				copies.push_back(std::move(copied));
			}
		}
		return copies;
	}
}

namespace Nyx::Editor
{
	SceneEditor::SceneEditor()
	{
		TransactionContext.ActiveScene = &Scene;
		Transactions.RegisterDomain(EObjectDomain::SceneEntity, &EntityDomain);
		Transactions.RegisterDomain(EObjectDomain::SceneOutline, &OutlineDomain);
		Transactions.RegisterDomain(EObjectDomain::Prefab, &PrefabDomain);
		PrefabDomain.SetAssets(&Assets);
		PrefabDomain.BeforeChange = [this](const std::string& prefabPath)
		{
			Prefabs.Remember(prefabPath);
		};
	}

	void SceneEditor::CancelPendingEdits()
	{
		DetailsContext.CancelPendingEdits();
		Gizmo.CancelInteraction();
		TerrainBrush.EndStroke(Scene, Transactions);
	}

	void SceneEditor::ForgetScene()
	{
		// Cancel before destroying the components captured by these unfinished edits.
		CancelPendingEdits();

		// Undo steps and the selection refer to entities by handle. In the new scene, the same
		// handles name other entities, so undo would change the wrong ones.
		Scene.Select(std::nullopt);
		Transactions.Clear();
		++SceneRevision;
	}

	void SceneEditor::CreateEntity(uint64_t folder)
	{
		const std::optional<Nyx::Entity> selected = Scene.GetSelection();
		const Nyx::EntityGuid after = selected ? Scene.GetGuid(*selected) : Nyx::EntityGuid{};
		const Nyx::Entity newEntity = Scene.CreateEntity("New Entity");
		Scene.Select(newEntity);
		// In prefab mode, where the root is, to be its child
		const std::optional<Nyx::Entity> prefabRoot = PrefabRoot.IsValid() ? Scene.FindEntity(PrefabRoot) : std::nullopt;
		if (prefabRoot && Scene.GetRegistry().Has<Nyx::TransformComponent>(*prefabRoot))
		{
			Scene.GetRegistry().Add<Nyx::TransformComponent>(newEntity, Scene.GetRegistry().Get<Nyx::TransformComponent>(*prefabRoot));
		}

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
		for (Change& placed : PlaceNewEntity(Scene, Scene.GetGuid(newEntity), after, folder))
		{
			transaction.Changes.push_back(std::move(placed));
		}
		if (prefabRoot && Scene.GetRegistry().Has<Nyx::TransformComponent>(newEntity))
		{
			ChangeParent(Scene, newEntity, *prefabRoot, transaction.Changes);
		}

		Transactions.Push(std::move(transaction));
	}

	void SceneEditor::DeleteSelectedEntity()
	{
		const std::vector<Nyx::Entity> selected = Scene.GetSelectedEntities();
		if (selected.empty())
		{
			return;
		}

		// With its children, at any depth: one undo brings them all back
		Transaction transaction{};
		transaction.Label = selected.size() > 1 ? "Delete Entities" : "Delete Entity";
		for (const Nyx::Entity root : selected)
		{
			// Gone already if it is below another that was selected
			if (!Scene.GetRegistry().IsAlive(root))
			{
				continue;
			}
			for (const Nyx::Entity entityToDelete : GetSelfAndBelow(Scene.GetRegistry(), root))
			{
				const ObjectRef rootRef = MakeSceneEntityRef(Scene, entityToDelete);
				RootObjectSnapshot snapshot = CaptureRootObjectSnapshot(EntityDomain, TransactionContext, rootRef);
				if (!Scene.DestroyEntity(entityToDelete))
				{
					continue;
				}

				Change change{};
				change.Kind = EChangeKind::DeleteObject;
				change.Payload = DeleteObjectChange{
					.Target = rootRef,
					.BeforeDelete = std::move(snapshot)
				};
				transaction.Changes.push_back(std::move(change));
			}
		}

		if (transaction.IsEmpty())
		{
			return;
		}
		Transactions.Push(std::move(transaction));
		Scene.Select(std::nullopt);
	}

	void SceneEditor::CopySelectedEntity()
	{
		std::vector<std::vector<std::byte>> copied = CopySelected(Scene);
		if (!copied.empty())
		{
			EntityClipboard = std::move(copied);
		}
	}

	void SceneEditor::DuplicateSelectedEntity()
	{
		// Like copy and paste, but the clipboard keeps what it had
		const std::vector<std::vector<std::byte>> copied = CopySelected(Scene);
		if (!copied.empty())
		{
			PasteAndSelect(copied, copied.size() > 1 ? "Duplicate Entities" : "Duplicate Entity");
		}
	}

	void SceneEditor::PasteEntity()
	{
		if (EntityClipboard.empty())
		{
			return;
		}

		PasteAndSelect(EntityClipboard, EntityClipboard.size() > 1 ? "Paste Entities" : "Paste Entity");
	}

	void SceneEditor::PasteAndSelect(const std::vector<std::vector<std::byte>>& copied, const char* label)
	{
		// A new entity can move components in memory; edits in progress point at components
		CancelPendingEdits();

		// Each right after the one before, the first after the selection
		const std::optional<Nyx::Entity> selected = Scene.GetSelection();
		Nyx::EntityGuid after = selected ? Scene.GetGuid(*selected) : Nyx::EntityGuid{};
		const auto place = [&](Nyx::Entity pasted, Transaction& step)
		{
			const Nyx::EntityGuid guid = Scene.GetGuid(pasted);
			for (Change& placed : PlaceNewEntity(Scene, guid, after, 0))
			{
				step.Changes.push_back(std::move(placed));
			}
			after = guid;
		};
		std::vector<Nyx::Entity> pasted = PasteEntities(Scene, Transactions, EntityDomain, TransactionContext, copied, label, place);
		if (!pasted.empty())
		{
			Scene.SelectEntities(std::move(pasted));
		}
	}

	void SceneEditor::Undo()
	{
		// The assets of what undo changed are loaded by AssetLoader
		const std::vector<Nyx::EntityGuid> selected = Scene.GetSelectedGuids();

		if (Transactions.Undo(TransactionContext) && !selected.empty())
		{
			Scene.SelectGuids(selected);
		}
	}

	void SceneEditor::Redo()
	{
		const std::vector<Nyx::EntityGuid> selected = Scene.GetSelectedGuids();

		if (Transactions.Redo(TransactionContext) && !selected.empty())
		{
			Scene.SelectGuids(selected);
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

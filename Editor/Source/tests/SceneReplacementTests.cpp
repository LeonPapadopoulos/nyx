#include "EditorLayer.h"
#include "ComponentRegistration.h"
#include "InspectorTargetIdHelpers.h"
#include "Log.h"
#include "NameComponent.h"
#include "SceneSerializer.h"
#include "TransactionObjectRefHelpers.h"
#include "VulkanImGuiBackend.h"
#include "VulkanRenderer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	class TestDirectory
	{
	public:
		TestDirectory()
		{
			const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (int attempt = 0; attempt < 64; ++attempt)
			{
				const auto candidate = std::filesystem::temp_directory_path() /
					("NyxEditorSceneTests-" + std::to_string(timestamp) + "-" + std::to_string(attempt));
				if (std::filesystem::create_directory(candidate))
				{
					Path = candidate;
					return;
				}
			}
			throw std::runtime_error("Could not create the test directory");
		}

		~TestDirectory()
		{
			// Only remove the directory this test created, never an existing directory.
			std::error_code ignored;
			std::filesystem::remove_all(Path, ignored);
		}

		std::filesystem::path Path;
	};
}

namespace Nyx::Tests
{
	using namespace Nyx::Engine;
	using namespace Nyx::Editor;

	// Seed interactions and GPU completion metadata without opening a window or allocating
	// Vulkan resources. The operations under test are the production editor/renderer methods.
	struct SceneReplacementTestAccess
	{
		static Entity Prepare(EditorLayer& editor, VulkanRenderer& renderer)
		{
			editor.Renderer = &renderer;
			editor.MainSceneViewId = 1;
			editor.SecondarySceneViewId = 2;
			renderer.SceneViews.resize(2);
			renderer.SceneViews[0].Id = 1;
			renderer.SceneViews[1].Id = 2;
			renderer.SetWorld(&editor.ActiveScene.GetRegistry());
			editor.Transactions.RegisterDomain(EObjectDomain::SceneEntity, &editor.SceneEntityDomain);
			editor.TransactionContext.ActiveScene = &editor.ActiveScene;

			const Entity entity = editor.ActiveScene.CreateEntity("Old scene");
			auto& world = editor.ActiveScene.GetRegistry();
			auto& transform = world.Add<TransformComponent>(entity, TransformComponent{});
			const ObjectRef target = MakeSceneEntityRef(entity);
			const auto& transformType = Reflection::GetTypeMetadata<TransformComponent>();

			// Keep entries in both undo and redo to test their lifetime across Open/New.
			TransactionDiffUtil history;
			history.TakeSnapshot(target, &transform, transformType);
			transform.Position.x = 1.0f;
			Require(history.CommitChanges("First move", editor.Transactions), "Could not seed undo");
			history.TakeSnapshot(target, &transform, transformType);
			transform.Position.x = 2.0f;
			Require(history.CommitChanges("Second move", editor.Transactions), "Could not seed redo");
			Require(editor.Transactions.Undo(editor.TransactionContext), "Could not seed redo history");

			auto& details = editor.DetailsPanelContext;
			details.Transactions = &editor.Transactions;
			details.CurrentObjectRef = target;
			details.CurrentTargetId = MakeInspectorTargetId(entity);
			details.GenericPropertyEdit.bEditing = true;
			details.GenericPropertyEdit.Target = target;
			details.GenericPropertyEdit.PendingDiff.emplace();
			details.GenericPropertyEdit.PendingDiff->TakeSnapshot(
				target, &world.Get<NameComponent>(entity), Reflection::GetTypeMetadata<NameComponent>());
			world.Get<NameComponent>(entity).Name = "Unfinished name";
			details.TransformRotationEdit.bEditing = true;
			details.TransformRotationEdit.Target = target;
			details.TransformRotationEdit.PendingDiff.emplace();
			details.TransformRotationEdit.PendingDiff->TakeSnapshot(target, &transform, transformType);
			details.TransformRotationEdit.CachedDegrees = glm::vec3(45.0f);

			auto& gizmo = editor.TransformGizmoInstance;
			gizmo.State.bDragging = true;
			gizmo.State.ActiveAxis = ETransformGizmoAxis::X;
			gizmo.State.HoveredAxis = ETransformGizmoAxis::X;
			gizmo.State.ActiveSceneViewId = editor.MainSceneViewId;
			gizmo.State.DragEntity = entity;
			gizmo.State.Operation = EGizmoOperation::Rotate;
			gizmo.State.Space = EGizmoSpace::World;
			gizmo.ActiveTransformDiff.emplace();
			gizmo.ActiveTransformDiff->TakeSnapshot(target, &transform, transformType);
			transform.Position.y = 3.0f;

			editor.ActiveScene.GetSelection() = entity;
			renderer.SetSelectedEntity(entity);
			editor.CurrentScenePath = "old.nyxscene";
			for (auto& view : renderer.SceneViews)
			{
				view.RenderedWorldRevision = renderer.WorldRevision;
				view.PickWorldRevision = renderer.WorldRevision;
				view.bPickRequestPending = true;
				view.bPickReadbackPending = true;
				view.bPickResultReady = true;
				view.LastPickedEntity = entity;
			}
			return entity;
		}

		static void RequireInteractionsCleared(EditorLayer& editor, VulkanRenderer& renderer)
		{
			Require(!editor.ActiveScene.GetSelection(), "Selection survived scene replacement");
			Require(!renderer.SelectedEntity, "Renderer kept the old selection outline");
			Require(!editor.Transactions.Undo(editor.TransactionContext), "Undo survived scene replacement");
			Require(!editor.Transactions.Redo(editor.TransactionContext), "Redo survived scene replacement");
			Require(editor.SceneRevision == 1, "Inspector widgets did not receive a new scene revision");

			const auto& details = editor.DetailsPanelContext;
			Require(!details.CurrentObjectRef.IsValid() && details.CurrentTargetId.Value == 0,
				"Inspector kept the old target");
			Require(!details.GenericPropertyEdit.bEditing && !details.GenericPropertyEdit.PendingDiff,
				"Inspector kept an unfinished edit");
			Require(!details.TransformRotationEdit.bEditing && !details.TransformRotationEdit.PendingDiff,
				"Inspector kept an unfinished rotation");
			Require(details.TransformRotationEdit.CachedDegrees == glm::vec3(0.0f), "Cached Euler angles survived");

			const auto& gizmo = editor.TransformGizmoInstance;
			Require(!gizmo.State.bDragging && !gizmo.ActiveTransformDiff, "Gizmo kept an unfinished drag");
			Require(gizmo.State.ActiveAxis == ETransformGizmoAxis::None &&
				gizmo.State.HoveredAxis == ETransformGizmoAxis::None && gizmo.State.ActiveSceneViewId == 0,
				"Gizmo retained an active handle or view");
			Require(gizmo.State.Operation == EGizmoOperation::Rotate && gizmo.State.Space == EGizmoSpace::World,
				"Cancelling the drag changed the user's tool preferences");

			Require(renderer.World == &editor.ActiveScene.GetRegistry() && renderer.WorldRevision == 2,
				"Replacing the registry at the same address did not advance the renderer revision");
			for (const auto& view : renderer.SceneViews)
			{
				Require(!view.bPickRequestPending && !view.bPickReadbackPending && !view.bPickResultReady,
					"A viewport kept an old pick");
				Require(!view.LastPickedEntity && view.RenderedWorldRevision == 0,
					"A viewport still represents the old scene as pickable");
			}
			editor.ApplyPendingPickResults();
			Require(!editor.ActiveScene.GetSelection(), "An old pick selected an entity after replacement");
		}

		static void TestNewScene()
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity oldEntity = Prepare(editor, renderer);
			Require(editor.NewScene(), "New failed");
			Require(!editor.ActiveScene.GetRegistry().IsAlive(oldEntity), "New kept the old world");
			Require(editor.CurrentScenePath.empty(), "New kept the old file path");
			RequireInteractionsCleared(editor, renderer);
		}

		static void TestOpenScene(const std::filesystem::path& path)
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity oldEntity = Prepare(editor, renderer);
			Require(editor.LoadCurrentScene(path), "Open failed");
			Require(editor.CurrentScenePath == path, "Open did not update the file path");
			auto& world = editor.ActiveScene.GetRegistry();
			Require(world.IsAlive(oldEntity) && world.Get<NameComponent>(oldEntity).Name == "Replacement",
				"Test scene must reuse the old handle for a different entity");
			RequireInteractionsCleared(editor, renderer);
			Require(world.Get<TransformComponent>(oldEntity).Position == glm::vec3(0.0f),
				"An unfinished drag changed the replacement entity");

			// A completed pick from the current revision must still select normally.
			auto& view = renderer.SceneViews[0];
			view.PickWorldRevision = renderer.WorldRevision;
			view.bPickResultReady = true;
			view.LastPickedEntity = oldEntity;
			editor.ApplyPendingPickResults();
			Require(editor.ActiveScene.GetSelection() == oldEntity, "New scene picking stopped working");
			Require(!renderer.ConsumeLastPickResult(view.Id).bHasNewResult, "Pick was delivered twice");
		}

		static void TestFailedOpen(const std::filesystem::path& path)
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity entity = Prepare(editor, renderer);
			auto* oldName = &editor.ActiveScene.GetRegistry().Get<NameComponent>(entity);
			Require(!editor.LoadCurrentScene(path), "An invalid scene loaded successfully");
			Require(&editor.ActiveScene.GetRegistry().Get<NameComponent>(entity) == oldName &&
				oldName->Name == "Unfinished name", "Failed Open replaced or reverted the old scene");
			Require(editor.CurrentScenePath == "old.nyxscene" && editor.SceneRevision == 0,
				"Failed Open changed the path or widget revision");
			Require(editor.ActiveScene.GetSelection() == entity && renderer.SelectedEntity == entity,
				"Failed Open cleared the selection");
			Require(renderer.WorldRevision == 1, "Failed Open invalidated the renderer revision");
			for (const auto& view : renderer.SceneViews)
			{
				Require(view.bPickRequestPending && view.bPickReadbackPending && view.bPickResultReady,
					"Failed Open discarded pending picks");
			}
			Require(editor.TransformGizmoInstance.State.bDragging && editor.TransformGizmoInstance.ActiveTransformDiff,
				"Failed Open cancelled the gizmo");
			Require(editor.DetailsPanelContext.GenericPropertyEdit.bEditing &&
				editor.DetailsPanelContext.GenericPropertyEdit.PendingDiff &&
				editor.DetailsPanelContext.TransformRotationEdit.bEditing &&
				editor.DetailsPanelContext.TransformRotationEdit.PendingDiff,
				"Failed Open cancelled pending inspector edits");
			Require(editor.Transactions.Redo(editor.TransactionContext), "Failed Open cleared redo");
			Require(editor.Transactions.Undo(editor.TransactionContext), "Could not undo the redone edit");
			Require(editor.Transactions.Undo(editor.TransactionContext), "Failed Open cleared undo");
			Require(editor.DetailsPanelContext.GenericPropertyEdit.PendingDiff->CommitChanges("Rename", editor.Transactions),
				"The pending inspector edit could not finish after failed Open");
		}

		static void TestLatePicks()
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity oldEntity = Prepare(editor, renderer);
			const uint64_t oldRevision = renderer.WorldRevision;
			Require(editor.NewScene(), "New failed");
			const Entity replacement = editor.ActiveScene.CreateEntity("Reused handle");
			Require(replacement == oldEntity, "Test must reuse the old entity handle");

			// Reject stale work at every stage, even if its handle is alive in the new scene.
			for (auto& view : renderer.SceneViews)
			{
				view.PickWorldRevision = oldRevision;
				view.bPickResultReady = true;
				view.LastPickedEntity = oldEntity;
				Require(!renderer.ConsumeLastPickResult(view.Id).bHasNewResult, "A stale result escaped");

				view.PickWorldRevision = oldRevision;
				view.bPickRequestPending = true;
				vk::raii::CommandBuffer unusedCommandBuffer{ nullptr };
				renderer.ResolvePickRequest(view, unusedCommandBuffer);
				Require(!view.bPickRequestPending && !view.bPickReadbackPending,
					"An old request reached the GPU copy stage");

				view.PickWorldRevision = oldRevision;
				view.bPickReadbackPending = true;
			}
			// No readback memory is allocated: attempting to map it would fail this test.
			renderer.ReadBackPickResults();
			editor.ApplyPendingPickResults();
			Require(!editor.ActiveScene.GetSelection(), "Late readback selected the reused handle");
			for (const auto& view : renderer.SceneViews)
			{
				Require(!view.bPickReadbackPending && !view.bPickResultReady, "Stale readback was not discarded");
			}
		}
	};
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		Nyx::Engine::RegisterComponentTypes();
		TestDirectory directory;
		const auto validPath = directory.Path / "replacement.nyxscene";
		Nyx::SceneDocument replacement;
		const auto entity = replacement.CreateEntity("Replacement");
		replacement.GetRegistry().Add<Nyx::Engine::TransformComponent>(entity, Nyx::Engine::TransformComponent{});
		Require(Nyx::Engine::SceneSerializer::SaveToFile(replacement.GetRegistry(), validPath), "Could not save fixture");
		const auto damagedPath = directory.Path / "damaged.nyxscene";
		std::filesystem::copy_file(validPath, damagedPath);
		std::filesystem::resize_file(damagedPath, std::filesystem::file_size(damagedPath) / 2);

		using Tests = Nyx::Tests::SceneReplacementTestAccess;
		Tests::TestNewScene();
		Tests::TestOpenScene(validPath);
		Tests::TestFailedOpen(directory.Path / "missing.nyxscene");
		Tests::TestFailedOpen(damagedPath);
		Tests::TestLatePicks();
		std::cout << "All editor scene replacement tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor scene replacement test failed: " << error.what() << '\n';
		return 1;
	}
}

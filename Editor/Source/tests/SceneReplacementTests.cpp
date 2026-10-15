#include "EditorLayer.h"
#include "ComponentRegistration.h"
#include "Log.h"
#include "NameComponent.h"
#include "SceneSerializer.h"
#include "TransactionObjectRefHelpers.h"
#include "VulkanImGuiBackend.h"
#include "VulkanRenderer.h"

#include "ComponentEdits.h"
#include "MeshRendererComponent.h"
#include "SceneEditor.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionUtils.h"
#include "TestSupport.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace
{
	using Nyx::Tests::Require;
	using Nyx::Tests::TestDirectory;

	// What the details panel's string widget was drawn with
	struct DrawnStringField
	{
		std::string OwnerType;
		std::string Property;
		Nyx::Editor::SubobjectPath Location;
	};

	std::vector<DrawnStringField> GDrawnStringFields;

	bool RecordStringField(const Nyx::Editor::PropertyWidgetArgs& args)
	{
		GDrawnStringFields.push_back(DrawnStringField{ args.OwnerType->Name, args.Property->Name, args.DrawContext->CurrentLocation });
		return false;
	}

	// A MeshRenderer's asset references are one row each, an asset picker, not their Type and
	// Path; assigning a material records where the reference is inside the component
	// (SubobjectPath), so undo finds it. Draws a MeshRenderer headless, then assigns a material.
	void TestDetailsPanelLocations()
	{
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(800, 600);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		Nyx::Editor::RegisterDefaultPropertyWidgets();
		Nyx::Editor::PropertyWidgetRegistry::Get().Register(Nyx::Reflection::EPropertyKind::String, &RecordStringField);

		Nyx::MeshRendererComponent meshRenderer{
			.Mesh = Nyx::AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" },
			.Material = Nyx::AssetReference{ .Type = "Material", .Path = "Materials/Textured.nyxmat" } };
		const Nyx::Reflection::TypeMetadata& meshRendererType = Nyx::Reflection::GetTypeMetadata<Nyx::MeshRendererComponent>();

		Nyx::Editor::InspectorDrawContext drawContext;
		drawContext.CurrentObjectRef = Nyx::Editor::MakeSceneEntityRef(Nyx::EntityGuid{ 42 });

		ImGui::NewFrame();
		ImGui::SetNextWindowSize(ImVec2(600, 500));
		ImGui::Begin("Details");
		Nyx::Editor::DrawReflectedTypeTable(&meshRenderer, meshRendererType, drawContext);
		ImGui::End();
		ImGui::Render();

		Nyx::Editor::RegisterDefaultPropertyWidgets();
		ImGui::DestroyContext();

		Require(std::none_of(GDrawnStringFields.begin(), GDrawnStringFields.end(), [](const DrawnStringField& field)
					{
						return field.Property == "Path" || field.Property == "Type";
					}),
			"An asset reference was drawn as its fields, not as a picker");
		Require(drawContext.CurrentLocation == Nyx::Editor::SubobjectPath{}, "The details panel left a struct's location behind");

		// Assigning a material: one undo step at the reference's place, which undo finds again
		Nyx::Editor::SceneEditor editing;
		const Nyx::Entity entity = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<Nyx::MeshRendererComponent>(entity, meshRenderer);
		Require(Nyx::Editor::AssignMaterial(editing.Scene, editing.Transactions, entity, "Materials/Stone.nyxmat"), "The material wasn't assigned");
		Require(!Nyx::Editor::AssignMaterial(editing.Scene, editing.Transactions, entity, "Materials/Stone.nyxmat"), "Assigning the material it has was a change");
		const Nyx::AssetReference& material = editing.Scene.GetRegistry().Get<Nyx::MeshRendererComponent>(entity).Material;
		Require(material.Type == "Material" && material.Path == "Materials/Stone.nyxmat", "The MeshRenderer doesn't name the assigned material");
		editing.Undo();
		Require(editing.Scene.GetRegistry().Get<Nyx::MeshRendererComponent>(entity).Material.Path == "Materials/Textured.nyxmat", "Undo didn't bring the material back");
		editing.Redo();
		Require(editing.Scene.GetRegistry().Get<Nyx::MeshRendererComponent>(entity).Material.Path == "Materials/Stone.nyxmat", "Redo didn't assign the material again");
	}
}

namespace Nyx::Tests
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	// Seed interactions and GPU completion metadata without opening a window or allocating
	// Vulkan resources. The operations under test are the production editor/renderer methods.
	struct SceneReplacementTestAccess
	{
		static Entity Prepare(EditorLayer& editor, VulkanRenderer& renderer)
		{
			editor.Renderer = &renderer;
			editor.MainSceneView.UseView(renderer, 1);
			editor.SecondarySceneView.UseView(renderer, 2);
			renderer.SceneViews.resize(2);
			renderer.SceneViews[0].Id = 1;
			renderer.SceneViews[1].Id = 2;
			renderer.SetWorld(&editor.Editing.Scene.GetRegistry());

			const Entity entity = editor.Editing.Scene.CreateEntity("Old scene");
			auto& world = editor.Editing.Scene.GetRegistry();
			auto& transform = world.Add<TransformComponent>(entity, TransformComponent{});
			const ObjectRef target = MakeSceneEntityRef(editor.Editing.Scene, entity);
			const auto& transformType = Reflection::GetTypeMetadata<TransformComponent>();

			// Keep entries in both undo and redo to test their lifetime across Open/New.
			TransactionDiffUtil history;
			history.TakeSnapshot(target, &transform, transformType);
			transform.Position.x = 1.0f;
			Require(history.CommitChanges("First move", editor.Editing.Transactions), "Could not seed undo");
			history.TakeSnapshot(target, &transform, transformType);
			transform.Position.x = 2.0f;
			Require(history.CommitChanges("Second move", editor.Editing.Transactions), "Could not seed redo");
			Require(editor.Editing.Transactions.Undo(editor.Editing.TransactionContext), "Could not seed redo history");

			auto& details = editor.Editing.DetailsContext;
			details.Transactions = &editor.Editing.Transactions;
			details.CurrentObjectRef = target;
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

			auto& gizmo = editor.Editing.Gizmo;
			gizmo.State.bDragging = true;
			gizmo.State.ActiveAxis = ETransformGizmoAxis::X;
			gizmo.State.HoveredAxis = ETransformGizmoAxis::X;
			gizmo.State.ActiveSceneViewId = editor.MainSceneView.GetViewId();
			gizmo.State.DragEntity = entity;
			gizmo.State.Operation = EGizmoOperation::Rotate;
			gizmo.State.Space = EGizmoSpace::World;
			gizmo.ActiveTransformDiff.emplace();
			gizmo.ActiveTransformDiff->TakeSnapshot(target, &transform, transformType);
			transform.Position.y = 3.0f;

			editor.Editing.Scene.GetSelection() = entity;
			renderer.SetSelectedEntity(entity);
			editor.CurrentScenePath = "old.nyxscene";
			for (auto& view : renderer.SceneViews)
			{
				view.RenderedWorldRevision = renderer.GetShownWorldRevision(view);
				view.PickWorldRevision = renderer.GetShownWorldRevision(view);
				view.bPickRequestPending = true;
				view.bPickReadbackPending = true;
				view.bPickResultReady = true;
				view.LastPickedEntity = entity;
			}
			return entity;
		}

		static void RequireInteractionsCleared(EditorLayer& editor, VulkanRenderer& renderer)
		{
			Require(!editor.Editing.Scene.GetSelection(), "Selection survived scene replacement");
			Require(!renderer.SelectedEntity, "Renderer kept the old selection outline");
			Require(!editor.Editing.Transactions.Undo(editor.Editing.TransactionContext), "Undo survived scene replacement");
			Require(!editor.Editing.Transactions.Redo(editor.Editing.TransactionContext), "Redo survived scene replacement");
			Require(editor.Editing.SceneRevision == 1, "Inspector widgets did not receive a new scene revision");

			const auto& details = editor.Editing.DetailsContext;
			Require(!details.CurrentObjectRef.IsValid(), "Inspector kept the old target");
			Require(!details.GenericPropertyEdit.bEditing && !details.GenericPropertyEdit.PendingDiff,
				"Inspector kept an unfinished edit");
			Require(!details.TransformRotationEdit.bEditing && !details.TransformRotationEdit.PendingDiff,
				"Inspector kept an unfinished rotation");
			Require(details.TransformRotationEdit.CachedDegrees == glm::vec3(0.0f), "Cached Euler angles survived");

			const auto& gizmo = editor.Editing.Gizmo;
			Require(!gizmo.State.bDragging && !gizmo.ActiveTransformDiff, "Gizmo kept an unfinished drag");
			Require(gizmo.State.ActiveAxis == ETransformGizmoAxis::None &&
				gizmo.State.HoveredAxis == ETransformGizmoAxis::None && gizmo.State.ActiveSceneViewId == 0,
				"Gizmo retained an active handle or view");
			Require(gizmo.State.Operation == EGizmoOperation::Rotate && gizmo.State.Space == EGizmoSpace::World,
				"Cancelling the drag changed the user's tool preferences");

			Require(renderer.World == &editor.Editing.Scene.GetRegistry() && renderer.GetShownWorldRevision(renderer.SceneViews[0]) == 2,
				"Replacing the registry at the same address did not advance the renderer revision");
			for (const auto& view : renderer.SceneViews)
			{
				Require(!view.bPickRequestPending && !view.bPickReadbackPending && !view.bPickResultReady,
					"A viewport kept an old pick");
				Require(!view.LastPickedEntity && view.RenderedWorldRevision == 0,
					"A viewport still represents the old scene as pickable");
			}
			editor.ApplyPendingPickResults();
			Require(!editor.Editing.Scene.GetSelection(), "An old pick selected an entity after replacement");
		}

		static void TestNewScene()
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity oldEntity = Prepare(editor, renderer);
			Require(editor.NewScene(), "New failed");
			Require(!editor.Editing.Scene.GetRegistry().IsAlive(oldEntity), "New kept the old world");
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
			auto& world = editor.Editing.Scene.GetRegistry();
			Require(world.IsAlive(oldEntity) && world.Get<NameComponent>(oldEntity).Name == "Replacement",
				"Test scene must reuse the old handle for a different entity");
			RequireInteractionsCleared(editor, renderer);
			Require(world.Get<TransformComponent>(oldEntity).Position == glm::vec3(0.0f),
				"An unfinished drag changed the replacement entity");

			// A completed pick from the current revision must still select normally.
			auto& view = renderer.SceneViews[0];
			view.PickWorldRevision = renderer.GetShownWorldRevision(renderer.SceneViews[0]);
			view.bPickResultReady = true;
			view.LastPickedEntity = oldEntity;
			editor.ApplyPendingPickResults();
			Require(editor.Editing.Scene.GetSelection() == oldEntity, "New scene picking stopped working");
			Require(!renderer.ConsumeLastPickResult(view.Id).bHasNewResult, "Pick was delivered twice");
		}

		static void TestFailedOpen(const std::filesystem::path& path)
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity entity = Prepare(editor, renderer);
			auto* oldName = &editor.Editing.Scene.GetRegistry().Get<NameComponent>(entity);
			Require(!editor.LoadCurrentScene(path), "An invalid scene loaded successfully");
			Require(&editor.Editing.Scene.GetRegistry().Get<NameComponent>(entity) == oldName &&
				oldName->Name == "Unfinished name", "Failed Open replaced or reverted the old scene");
			Require(editor.CurrentScenePath == "old.nyxscene" && editor.Editing.SceneRevision == 0,
				"Failed Open changed the path or widget revision");
			Require(editor.Editing.Scene.GetSelection() == entity && renderer.SelectedEntity == entity,
				"Failed Open cleared the selection");
			Require(renderer.GetShownWorldRevision(renderer.SceneViews[0]) == 1, "Failed Open invalidated the renderer revision");
			for (const auto& view : renderer.SceneViews)
			{
				Require(view.bPickRequestPending && view.bPickReadbackPending && view.bPickResultReady,
					"Failed Open discarded pending picks");
			}
			Require(editor.Editing.Gizmo.State.bDragging && editor.Editing.Gizmo.ActiveTransformDiff,
				"Failed Open cancelled the gizmo");
			Require(editor.Editing.DetailsContext.GenericPropertyEdit.bEditing &&
				editor.Editing.DetailsContext.GenericPropertyEdit.PendingDiff &&
				editor.Editing.DetailsContext.TransformRotationEdit.bEditing &&
				editor.Editing.DetailsContext.TransformRotationEdit.PendingDiff,
				"Failed Open cancelled pending inspector edits");
			Require(editor.Editing.Transactions.Redo(editor.Editing.TransactionContext), "Failed Open cleared redo");
			Require(editor.Editing.Transactions.Undo(editor.Editing.TransactionContext), "Could not undo the redone edit");
			Require(editor.Editing.Transactions.Undo(editor.Editing.TransactionContext), "Failed Open cleared undo");
			Require(editor.Editing.DetailsContext.GenericPropertyEdit.PendingDiff->CommitChanges("Rename", editor.Editing.Transactions),
				"The pending inspector edit could not finish after failed Open");
		}

		static void TestLatePicks()
		{
			VulkanRenderer renderer;
			EditorLayer editor;
			const Entity oldEntity = Prepare(editor, renderer);
			const uint64_t oldRevision = renderer.GetShownWorldRevision(renderer.SceneViews[0]);
			Require(editor.NewScene(), "New failed");
			const Entity replacement = editor.Editing.Scene.CreateEntity("Reused handle");
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
			Require(!editor.Editing.Scene.GetSelection(), "Late readback selected the reused handle");
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
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestDirectory directory("NyxEditorSceneTests");
		const auto validPath = directory.Path / "replacement.nyxscene";
		Nyx::SceneDocument replacement;
		const auto entity = replacement.CreateEntity("Replacement");
		replacement.GetRegistry().Add<Nyx::TransformComponent>(entity, Nyx::TransformComponent{});
		Require(Nyx::SceneSerializer::SaveToFile(replacement.GetRegistry(), validPath), "Could not save fixture");
		const auto damagedPath = directory.Path / "damaged.nyxscene";
		std::filesystem::copy_file(validPath, damagedPath);
		std::filesystem::resize_file(damagedPath, std::filesystem::file_size(damagedPath) / 2);

		using Tests = Nyx::Tests::SceneReplacementTestAccess;
		Tests::TestNewScene();
		Tests::TestOpenScene(validPath);
		Tests::TestFailedOpen(directory.Path / "missing.nyxscene");
		Tests::TestFailedOpen(damagedPath);
		Tests::TestLatePicks();
		TestDetailsPanelLocations();
		std::cout << "All editor scene replacement tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor scene replacement test failed: " << error.what() << '\n';
		return 1;
	}
}

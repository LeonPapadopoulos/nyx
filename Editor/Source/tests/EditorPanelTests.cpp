// The pieces EditorLayer is built from, without a window or renderer: SceneEditor's commands and
// hotkeys, and the panels drawn headless with ImGui.
#include "CameraComponent.h"
#include "ComponentEdits.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "CrashCardsPanel.h"
#include "DetailsPanel.h"
#include "EditorPreferences.h"
#include "EntityReferenceWidget.h"
#include "GroundComponent.h"
#include "GuidComponent.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "NameComponent.h"
#include "Painter.h"
#include "PainterTestWindow.h"
#include "PhysicsComponents.h"
#include "PlayControls.h"
#include "PlaySession.h"
#include "AssetDatabase.h"
#include "AssetTypeRegistry.h"
#include "GameLinkPanel.h"
#include "EditorCameraController.h"
#include "InputAssetDocument.h"
#include "InputPanel.h"
#include "InputRecording.h"
#include "VirtualInputBackend.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectionSourceRegistry.h"
#include "ReflectionUtils.h"
#include "MeshInstancesComponent.h"
#include "RuntimeMeshComponent.h"
#include "ScatterBlockerComponent.h"
#include "ScatterComponent.h"
#include "ScatterInstances.h"
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneOutlinerPanel.h"
#include "SceneSerializer.h"
#include "SurfaceDecalComponent.h"
#include "ThemeEditorWindow.h"
#include "TransactionDiffUtil.h"
#include "TransactionObjectRefHelpers.h"
#include "TransformComponent.h"
#include "UIAnimation.h"
#include "UIChrome.h"
#include "UIDiagnostics.h"
#include "UITheme.h"
#include "UIThemeFile.h"
#include "UIWidgets.h"
#include "WindowPainter.h"
#include "HeadlessUI.h"
#include "TestSupport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::UI;
	using namespace Nyx::Editor;

	using Nyx::Tests::HeadlessUI;
	using Nyx::Tests::Require;

	size_t CountEntities(const SceneEditor& editing)
	{
		size_t count = 0;
		editing.Scene.GetRegistry().ForEachEntity([&](Entity)
			{
				++count;
			});
		return count;
	}

	std::string GetName(const SceneEditor& editing, Entity entity)
	{
		return editing.Scene.GetRegistry().Get<NameComponent>(entity).Name;
	}

	void TestCreateAndDeleteAreUndoable()
	{
		SceneEditor editing;

		editing.CreateEntity();
		Require(CountEntities(editing) == 1, "Add Entity should make one entity");
		Require(editing.Scene.GetSelection().has_value(), "A new entity should be selected");
		const EntityGuid guid = editing.Scene.GetGuid(*editing.Scene.GetSelection());

		editing.DeleteSelectedEntity();
		Require(CountEntities(editing) == 0 && !editing.Scene.GetSelection(), "Delete should remove and deselect the entity");

		editing.Undo();
		Require(editing.Scene.FindEntity(guid).has_value(), "Undoing Delete should bring the entity back with its guid");

		editing.Undo();
		Require(CountEntities(editing) == 0, "Undoing Add Entity should remove it again");

		editing.Redo();
		Require(editing.Scene.FindEntity(guid).has_value(), "Redoing Add Entity should bring it back");
	}

	void TestSelectionFollowsGuid()
	{
		SceneEditor editing;
		editing.CreateEntity();
		const EntityGuid firstGuid = editing.Scene.GetGuid(*editing.Scene.GetSelection());
		editing.DeleteSelectedEntity();

		// Takes the deleted entity's slot, so undo brings it back in another one
		const Entity other = editing.Scene.CreateEntity("Other");
		editing.Scene.GetSelection() = other;

		editing.Undo();
		Require(editing.Scene.GetSelection() == other, "Undo should keep the selection on the same entity");

		const std::optional<Entity> restored = editing.Scene.FindEntity(firstGuid);
		Require(restored.has_value() && *restored != other, "Undo should restore the deleted entity in a free slot");

		editing.Scene.GetSelection() = *restored;
		editing.Redo();
		Require(!editing.Scene.GetSelection(), "Redoing the delete should clear the selection of the deleted entity");
	}

	void TestDuplicateCopyAndPaste()
	{
		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});
		editing.Scene.GetSelection() = cube;

		Require(!editing.CanPaste(), "Nothing should be pasteable before a copy");
		editing.PasteEntity();
		Require(CountEntities(editing) == 1, "Paste with an empty clipboard should do nothing");

		editing.DuplicateSelectedEntity();
		const Entity duplicate = *editing.Scene.GetSelection();
		Require(duplicate != cube && GetName(editing, duplicate) == "Cube (2)", "Duplicate should select a renamed copy");
		Require(editing.Scene.GetGuid(duplicate) != editing.Scene.GetGuid(cube), "A duplicate should get a new guid");
		Require(!editing.CanPaste(), "Duplicate should leave the clipboard alone");

		editing.Scene.GetSelection() = cube;
		editing.CopySelectedEntity();
		Require(editing.CanPaste(), "Copy should fill the clipboard");
		editing.PasteEntity();
		Require(CountEntities(editing) == 3 && GetName(editing, *editing.Scene.GetSelection()) == "Cube (3)",
			"Paste should select a renamed copy");

		editing.Undo();
		Require(CountEntities(editing) == 2, "Paste should be undoable");
	}

	void TestForgetScene()
	{
		SceneEditor editing;
		editing.CreateEntity();

		editing.ForgetScene();

		Require(!editing.Scene.GetSelection(), "Forgetting the scene should clear the selection");
		Require(editing.SceneRevision == 1, "Forgetting the scene should count a new revision");
		Require(!editing.Transactions.Undo(editing.TransactionContext), "Forgetting the scene should clear undo");
	}

	void TestHotkeys()
	{
		HeadlessUI ui;
		SceneEditor editing;
		const auto handle = [&]()
		{
			editing.HandleHotkeys();
		};

		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetSelection() = cube;

		ui.Press(ImGuiKey_D, true, false, handle);
		Require(CountEntities(editing) == 2, "Ctrl+D should duplicate");

		ui.Press(ImGuiKey_Z, true, false, handle);
		Require(CountEntities(editing) == 1, "Ctrl+Z should undo");

		ui.Press(ImGuiKey_Z, true, true, handle);
		Require(CountEntities(editing) == 2, "Ctrl+Shift+Z should redo");

		ui.Press(ImGuiKey_Z, true, false, handle);
		ui.Press(ImGuiKey_Y, true, false, handle);
		Require(CountEntities(editing) == 2, "Ctrl+Y should redo");

		editing.Scene.GetSelection() = cube;
		ui.Press(ImGuiKey_C, true, false, handle);
		ui.Press(ImGuiKey_V, true, false, handle);
		Require(CountEntities(editing) == 3 && editing.CanPaste(), "Ctrl+C and Ctrl+V should copy and paste");
	}

	void TestPanelsDrawHeadless()
	{
		HeadlessUI ui;
		RegisterDefaultPropertyWidgets();

		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});

		SceneOutlinerPanel outliner;
		DetailsPanel details;

		// Nothing selected, then the cube, then an entity that was deleted meanwhile
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				details.Draw(editing);
			});

		editing.Scene.GetSelection() = cube;
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				details.Draw(editing);
			});
		Require(editing.DetailsContext.CurrentObjectRef.IsValid(), "The details panel should target the selected entity");

		editing.Scene.DestroyEntity(cube);
		ui.Frame([&]()
			{
				details.Draw(editing);
			});
		Require(!editing.Scene.GetSelection(), "The details panel should drop a selection that is gone");

		// A closed panel draws nothing
		outliner.bOpen = false;
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				Require(ImGui::FindWindowByName("Scene Outliner") == nullptr || !ImGui::FindWindowByName("Scene Outliner")->Active,
					"A closed outliner should not be drawn");
			});
	}

	// Docked panels can be made as narrow as Nyx::UI::DockedWindowMinWidth: by dragging the splitter
	// beside them, and in a split without the dock space's middle, where ImGui gives each side its
	// minimum. What they hold is cut off; it doesn't hold them wide.
	void TestPanelsDockNarrow()
	{
		HeadlessUI ui;
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		RegisterDefaultPropertyWidgets();

		// A selected entity, so the details show its components' tables
		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("A cube with a long name that would want room");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});
		editing.Scene.GetSelection() = cube;
		SceneOutlinerPanel outliner;
		DetailsPanel details;
		GameLinkPanel gameLink;
		bool bGameLinkOpen = true;

		// The outliner on the left, the middle empty, and on the right the details beside the Game
		// Link, in a split of their own
		bool bBuilt = false;
		const auto frame = [&]()
		{
			ui.Frame([&]()
				{
					const ImGuiViewport* viewport = ImGui::GetMainViewport();
					ImGui::SetNextWindowPos(viewport->Pos);
					ImGui::SetNextWindowSize(viewport->Size);
					ImGui::Begin("DockSpaceWindow", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
					const ImGuiID dockSpace = ImGui::GetID("NarrowDockSpace");
					Nyx::UI::DockSpace(dockSpace);
					if (!bBuilt)
					{
						bBuilt = true;
						ImGui::DockBuilderRemoveNodeChildNodes(dockSpace);
						ImGui::DockBuilderSetNodeSize(dockSpace, viewport->Size);
						ImGuiID middle = dockSpace;
						const ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.2f, nullptr, &middle);
						ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.7f, nullptr, &middle);
						const ImGuiID rightOfRight = ImGui::DockBuilderSplitNode(right, ImGuiDir_Right, 0.5f, nullptr, &right);
						ImGui::DockBuilderDockWindow("Scene Outliner", left);
						ImGui::DockBuilderDockWindow("Details", right);
						ImGui::DockBuilderDockWindow("Game Link", rightOfRight);
						ImGui::DockBuilderFinish(dockSpace);
					}
					ImGui::End();
					outliner.Draw(editing);
					details.Draw(editing);
					gameLink.Draw(bGameLinkOpen);
				});
		};
		for (int index = 0; index < 3; ++index)
		{
			frame();
		}
		ImGuiWindow* outlinerWindow = ImGui::FindWindowByName("Scene Outliner");
		ImGuiWindow* detailsWindow = ImGui::FindWindowByName("Details");
		Require(outlinerWindow && outlinerWindow->DockNode && detailsWindow && detailsWindow->DockNode, "The outliner and the details should be docked");
		Require(outlinerWindow->Size.x > 100.0f && detailsWindow->Size.x > 100.0f, "The panels should start wide");

		// The splitter right of the outliner, dragged to the window's left edge
		ImGuiIO& io = ImGui::GetIO();
		const float splitterX = outlinerWindow->DockNode->Pos.x + outlinerWindow->DockNode->Size.x + ImGui::GetStyle().DockingSeparatorSize * 0.5f;
		const float splitterY = outlinerWindow->DockNode->Pos.y + outlinerWindow->DockNode->Size.y * 0.5f;
		io.AddMousePosEvent(splitterX, splitterY);
		frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		frame();
		for (float x = splitterX; x > 0.0f; x -= 40.0f)
		{
			io.AddMousePosEvent(x, splitterY);
			frame();
		}
		io.AddMousePosEvent(0.0f, splitterY);
		frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		frame();
		frame();
		Require(outlinerWindow->Size.x <= Nyx::UI::DockedWindowMinWidth + 2.0f,
			"Dragging the splitter should make the outliner as narrow as " + std::to_string(static_cast<int>(Nyx::UI::DockedWindowMinWidth)) + " pixels, not " + std::to_string(static_cast<int>(outlinerWindow->Size.x)));

		// The details' side of their split asked to be next to nothing
		detailsWindow->DockNode->SizeRef.x = 1.0f;
		frame();
		frame();
		Require(detailsWindow->Size.x <= Nyx::UI::DockedWindowMinWidth + 2.0f,
			"The details should be as narrow as " + std::to_string(static_cast<int>(Nyx::UI::DockedWindowMinWidth)) + " pixels in a split of their own, not " + std::to_string(static_cast<int>(detailsWindow->Size.x)));
		Require(detailsWindow->Size.x >= Nyx::UI::DockedWindowMinWidth - 1.0f, "The details should keep their minimum width");

		Require(ImGui::FindWindowByName("Game Link")->Size.x > 100.0f, "The Game Link should take the room the details left");
	}

	// A mesh made at run time isn't reflected: the details show what it is, and draw it with the
	// entity's components
	void TestRuntimeMeshDetails()
	{
		HeadlessUI ui;
		RegisterDefaultPropertyWidgets();

		SceneEditor editing;
		const Entity ground = editing.Scene.CreateEntity("Ground");
		RuntimeMeshComponent runtimeMesh;
		Require(DescribeRuntimeMesh(runtimeMesh) == std::vector<std::string>{ "No mesh yet" }, "A run-time mesh without data should say so");

		auto mesh = std::make_shared<MeshData>();
		mesh->Vertices.resize(6);
		mesh->Indices = { 0, 1, 2, 3, 4, 5 };
		mesh->Sections = { MeshSection{ .FirstIndex = 0, .IndexCount = 3, .MaterialSlot = 0 }, MeshSection{ .FirstIndex = 3, .IndexCount = 3, .MaterialSlot = 1 } };
		SetRuntimeMesh(runtimeMesh, mesh);
		const std::vector<std::string> lines = DescribeRuntimeMesh(runtimeMesh);
		Require(lines.size() == 4 && lines[0] == "6 vertices, 2 triangles" && lines[1] == "Section 0: 1 triangles, material slot 0" &&
				lines[2] == "Section 1: 1 triangles, material slot 1" && lines[3] == "Revision 1",
			"The run-time mesh's details should list its counts and sections");

		mesh->Indices[5] = 6;
		Require(DescribeRuntimeMesh(runtimeMesh)[3].starts_with("Not drawn: "), "A run-time mesh that isn't valid should say why it isn't drawn");

		MeshInstancesComponent meshInstances;
		Require(DescribeMeshInstances(meshInstances) == std::vector<std::string>{ "No instances yet" }, "Mesh instances without data should say so");
		auto instances = std::make_shared<MeshInstances>();
		instances->Transforms.resize(5);
		SetMeshInstances(meshInstances, instances);
		Require(DescribeMeshInstances(meshInstances) == std::vector<std::string>{ "5 instances in one cell", "Revision 1" }, "Mesh instances without cells should be one cell");
		instances->Cells.resize(2);
		Require(DescribeMeshInstances(meshInstances)[0] == "5 instances in 2 cells", "Mesh instances should say how many cells they are in");

		editing.Scene.GetRegistry().Add<RuntimeMeshComponent>(ground, runtimeMesh);
		editing.Scene.GetSelection() = ground;
		DetailsPanel details;
		ui.Frame([&]()
			{
				details.Draw(editing);
			});
		ui.Frame([&]()
			{
				details.Draw(editing);
			});
	}

	// What the camera's post-load step was given, each time it ran
	struct PostLoadCall
	{
		Registry* World = nullptr;
		Nyx::Entity Entity{};
		float FovYDegrees = 0.0f;
	};
	std::vector<PostLoadCall> PostLoadCalls;

	void RecordPostLoad(CameraComponent& camera, ScenePostLoadContext& context)
	{
		// Only the camera that is the entity's, in that world, counts
		if (context.World && context.World->Has<CameraComponent>(context.Entity) && &context.World->Get<CameraComponent>(context.Entity) == &camera)
		{
			PostLoadCalls.push_back(PostLoadCall{ context.World, context.Entity, camera.FovYDegrees });
		}
	}

	// The editor runs a component's post-load step after it is added, edited and undone
	// (ComponentPostLoadSubscriber), with the scene's world and the component's entity
	void TestPostLoadStepsKnowTheirEntity()
	{
		ComponentTypeRegistry::Get().SetPostLoad<CameraComponent>(&RecordPostLoad);
		SceneEditor editing;
		editing.Transactions.Subscribe(&editing.AssetLoader);
		Registry& world = editing.Scene.GetRegistry();
		editing.Scene.CreateEntity("Cube");
		const Entity camera = editing.Scene.CreateEntity("Camera");
		const ComponentTypeOps& cameraType = *ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<CameraComponent>());
		const auto ranFor = [&](float fovYDegrees)
		{
			return PostLoadCalls.size() == 1 && PostLoadCalls[0].World == &world && PostLoadCalls[0].Entity == camera &&
				PostLoadCalls[0].FovYDegrees == fovYDegrees;
		};

		PostLoadCalls.clear();
		Require(AddComponent(editing.Scene, editing.Transactions, camera, cameraType), "Adding a camera should be an edit");
		Require(ranFor(CameraComponent{}.FovYDegrees), "Adding a component should run its post-load step for its entity, in the scene's world");

		// As the details panel edits a value
		TransactionDiffUtil diff;
		diff.TakeSnapshot(MakeSceneEntityRef(editing.Scene, camera), &world.Get<CameraComponent>(camera), Nyx::Reflection::GetTypeMetadata<CameraComponent>());
		world.Get<CameraComponent>(camera).FovYDegrees = 80.0f;
		PostLoadCalls.clear();
		Require(diff.CommitChanges("Edit Property", editing.Transactions), "Changing the field of view should be an edit");
		Require(ranFor(80.0f), "Editing a component should run its post-load step for its entity, with the new values");

		PostLoadCalls.clear();
		editing.Undo();
		Require(ranFor(CameraComponent{}.FovYDegrees), "Undoing an edit should run the post-load step with the values as they were");
	}

	// A ground in the editor, through its post-load step as the editor runs it after each edit, undo
	// and redo: added, it has its mesh; edited, a new one; removed, none; undone, its mesh again
	void TestGroundFollowsEdits()
	{
		SceneEditor editing;
		editing.Transactions.Subscribe(&editing.AssetLoader);
		Registry& world = editing.Scene.GetRegistry();
		const Entity ground = editing.Scene.CreateEntity("Ground");
		const ComponentTypeOps& groundType = *ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<GroundComponent>());

		Require(AddComponent(editing.Scene, editing.Transactions, ground, groundType), "Adding a ground should be an edit");
		Require(world.Has<RuntimeMeshComponent>(ground), "An added ground should have its mesh in the editor");
		const RuntimeMeshComponent added = world.Get<RuntimeMeshComponent>(ground);

		// As the details panel edits a value
		TransactionDiffUtil diff;
		diff.TakeSnapshot(MakeSceneEntityRef(editing.Scene, ground), &world.Get<GroundComponent>(ground), Nyx::Reflection::GetTypeMetadata<GroundComponent>());
		world.Get<GroundComponent>(ground).Seed = 7;
		Require(diff.CommitChanges("Edit Property", editing.Transactions), "Changing the seed should be an edit");
		const RuntimeMeshComponent& edited = world.Get<RuntimeMeshComponent>(ground);
		Require(edited.Mesh != added.Mesh && edited.Revision > added.Revision, "An edited ground should be made again with a new revision");

		editing.Undo();
		Require(world.Get<GroundComponent>(ground).Seed == 1 && world.Get<RuntimeMeshComponent>(ground).Mesh->Vertices[500].Position == added.Mesh->Vertices[500].Position,
			"Undoing the edit should make the ground as it was");

		Require(RemoveComponent(editing.Scene, editing.Transactions, ground, groundType), "Removing the ground should be an edit");
		Require(!world.Has<RuntimeMeshComponent>(ground), "A removed ground should take its mesh with it");
		editing.Undo();
		Require(world.Has<GroundComponent>(ground) && world.Has<RuntimeMeshComponent>(ground), "Undoing the removal should bring the ground and its mesh back");
	}

	// A scatter and a blocker in the editor, followed each frame as EditorLayer::OnUpdate() follows
	// them: a blocker added takes the copies round it away the next frame, dragged it makes only the
	// cells it reaches again, and undone the copies come back as they were
	void TestScatterFollowsBlockers()
	{
		SceneEditor editing;
		editing.Transactions.Subscribe(&editing.AssetLoader);
		Registry& world = editing.Scene.GetRegistry();
		const Entity meadow = editing.Scene.CreateEntity("Meadow");
		const ComponentTypeOps& scatterType = *ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<ScatterComponent>());
		Require(AddComponent(editing.Scene, editing.Transactions, meadow, scatterType), "Adding a scatter should be an edit");
		world.Get<ScatterComponent>(meadow).Size = glm::vec2(64.0f, 64.0f);
		world.Get<ScatterComponent>(meadow).CellSize = 8.0f;
		world.Get<ScatterComponent>(meadow).Density = 2.0f;
		UpdateScatters(world);
		const std::vector<glm::mat4> unblocked = world.Get<MeshInstancesComponent>(meadow).Instances->Transforms;

		// A crate scaled six times: its blocker, without a collider, is a circle of 0.5 m times 6
		const Entity crate = editing.Scene.CreateEntity("Crate");
		world.Add<TransformComponent>(crate, TransformComponent{ .Scale = glm::vec3(6.0f) });
		const ComponentTypeOps& blockerType = *ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<ScatterBlockerComponent>());
		const auto copiesRound = [&](float x)
		{
			return std::count_if(world.Get<MeshInstancesComponent>(meadow).Instances->Transforms.begin(), world.Get<MeshInstancesComponent>(meadow).Instances->Transforms.end(),
				[x](const glm::mat4& transform)
				{
					return glm::length(glm::vec2(transform[3].x - x, transform[3].z)) < 3.1f;
				});
		};
		Require(copiesRound(0.0f) > 10, "The meadow should have copies where the crate is");
		Require(AddComponent(editing.Scene, editing.Transactions, crate, blockerType), "Adding a scatter blocker should be an edit");
		UpdateScatters(world);
		Require(copiesRound(0.0f) == 0, "An added blocker should take the copies round it away the next frame");

		// Dragged by the gizmo, a frame at a time
		for (int frame = 1; frame <= 4; ++frame)
		{
			world.Get<TransformComponent>(crate).Position.x = 0.5f * static_cast<float>(frame);
			UpdateScatters(world);
			Require(copiesRound(0.5f * static_cast<float>(frame)) == 0, "A dragged blocker should keep the copies away where it is");
			Require(world.Get<ScatterComponent>(meadow).Cells->CellsThrown <= 4, "A dragged blocker should make only the cells it reaches again");
		}

		editing.Undo();
		Require(!world.Has<ScatterBlockerComponent>(crate), "Undoing should take the blocker away");
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(meadow).Instances->Transforms == unblocked, "Without the blocker, the copies should be as they were");
	}

	// ImGui counts the items that share the ID of the one under the mouse ("visible items with
	// conflicting ID"). The mouse goes down the details panel row by row, over an entity with every
	// physics component: a category named like its component ("Rigid Body") or a property must
	// never share an item's ID.
	void TestDetailsHaveNoConflictingIds()
	{
		HeadlessUI ui;
		RegisterDefaultPropertyWidgets();

		SceneEditor editing;
		Registry& world = editing.Scene.GetRegistry();
		const Entity crate = editing.Scene.CreateEntity("Crate");
		world.Add<TransformComponent>(crate, TransformComponent{});
		world.Add<BoxColliderComponent>(crate, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(crate, RigidBodyComponent{});
		world.Add<PhysicsMaterialComponent>(crate, PhysicsMaterialComponent{});
		editing.Scene.GetSelection() = crate;

		DetailsPanel details;
		const auto draw = [&]()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 780.0f));
			details.Draw(editing);
		};
		ui.Frame(draw);
		ui.Frame(draw);

		for (float y = 0.0f; y < 780.0f; y += 3.0f)
		{
			ImGui::GetIO().AddMousePosEvent(40.0f, y);
			ui.Frame(draw);
			ui.Frame(draw);
			Require(ImGui::GetCurrentContext()->HoveredIdPreviousFrameItemCount <= 1,
				"Two items in the details panel share an ID (under the mouse at y = " + std::to_string(y) + ")");
		}
	}

	// A cube and a decal, the decal selected: its details at the top left, the outliner beside them.
	// The items of the decal's host field are found through the UI source inspector's callback, which
	// reports each item with the property it belongs to; the host's declaration is marked for that.
	class EntityFieldFixture
	{
	public:
		EntityFieldFixture()
		{
			RegisterDefaultPropertyWidgets();
			Registry& world = Editing.Scene.GetRegistry();
			Cube = Editing.Scene.CreateEntity("Cube");
			world.Add<TransformComponent>(Cube, TransformComponent{});
			Decal = Editing.Scene.CreateEntity("Decal");
			world.Add<SurfaceDecalComponent>(Decal, SurfaceDecalComponent{});
			Editing.Scene.GetSelection() = Decal;

			ReflectionSourceRegistry::Get().Register(GetHostProperty(), SourceLocation(HostMarker, 1));
			Nyx::UI::SetSourceItemCallback(&Collect, this);
			Nyx::UI::SetInspectingSources(true);
		}

		~EntityFieldFixture()
		{
			Nyx::UI::SetInspectingSources(false);
			Nyx::UI::SetSourceItemCallback(nullptr, nullptr);
			ReflectionSourceRegistry::Get().Register(GetHostProperty(), SourceLocation{});
		}

		EntityFieldFixture(const EntityFieldFixture&) = delete;
		EntityFieldFixture& operator=(const EntityFieldFixture&) = delete;

		void Draw()
		{
			Items.clear();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 780.0f));
			Details.Draw(Editing);
			ImGui::SetNextWindowPos(ImVec2(420.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 400.0f));
			Outliner.Draw(Editing);
		}

		void Frame()
		{
			UI.Frame([&]()
				{
					Draw();
				});
		}

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Frame();
		}

		void Press(ImVec2 at)
		{
			MoveTo(at);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		}

		// Opens the host's combo; a popup is placed in the frames after it opens
		void OpenCombo()
		{
			Press(GetHostItems().back().GetCenter());
			Frame();
			Frame();
		}

		void DragTo(ImVec2 from, ImVec2 to)
		{
			MoveTo(from);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
			for (int step = 1; step <= 4; ++step)
			{
				MoveTo(ImVec2(from.x + (to.x - from.x) * step / 4.0f, from.y + (to.y - from.y) * step / 4.0f));
			}
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
			Frame();
		}

		EntityGuid GetHost() const
		{
			return Editing.Scene.GetRegistry().Get<SurfaceDecalComponent>(Decal).Anchor.Host;
		}

		// The host field's items in the details: the eyedropper, the select button, the combo
		std::vector<ImRect> GetHostItems() const
		{
			return Find([](const Item& item)
				{
					return item.bHost && item.WindowName == "Details";
				});
		}

		// The combo's list, while it is open: the filter, None, then the entities. The combo itself
		// comes first in the list's window, as it is reported once its list has begun.
		std::vector<ImRect> GetListItems() const
		{
			std::vector<ImRect> items = Find([](const Item& item)
				{
					return item.bHost && item.WindowName.starts_with("##Combo");
				});
			if (!items.empty())
			{
				items.erase(items.begin());
			}
			return items;
		}

		// The outliner's rows are the last of its items, in the order the entities were made
		ImRect GetOutlinerRow(Entity entity) const
		{
			const std::vector<ImRect> items = Find([](const Item& item)
				{
					return item.WindowName == "Scene Outliner";
				});
			const size_t rows = 2;
			Require(items.size() >= rows, "The outliner should show both entities");
			return items[items.size() - rows + (entity == Cube ? 0 : 1)];
		}

		HeadlessUI UI;
		SceneEditor Editing;
		DetailsPanel Details;
		SceneOutlinerPanel Outliner;
		Entity Cube;
		Entity Decal;

	private:
		static constexpr const char* HostMarker = "EntityFieldFixture: SurfaceAnchor::Host";

		struct Item
		{
			std::string WindowName;
			bool bHost = false;
			bool bWindow = false;
			ImRect Rect;
		};

		static const Nyx::Reflection::PropertyMetadata& GetHostProperty()
		{
			return Nyx::Reflection::GetTypeMetadata<SurfaceAnchor>().Properties[0];
		}

		static void Collect(const Nyx::UI::SourceItem& item, void* userData)
		{
			EntityFieldFixture& fixture = *static_cast<EntityFieldFixture*>(userData);
			fixture.Items.push_back(Item{ item.Window ? std::string(item.Window->Name) : std::string(),
				std::string_view(item.Declaration.File) == HostMarker, item.bWindow, ImRect(item.Min, item.Max) });
		}

		template<typename Predicate>
		std::vector<ImRect> Find(Predicate predicate) const
		{
			std::vector<ImRect> found;
			for (const Item& item : Items)
			{
				if (!item.bWindow && predicate(item))
				{
					found.push_back(item.Rect);
				}
			}
			return found;
		}

		std::vector<Item> Items;
	};

	// The host field: choosing from its combo, its filter, None, and undo of each
	void TestEntityReferenceCombo()
	{
		EntityFieldFixture fixture;
		const EntityGuid cubeGuid = fixture.Editing.Scene.GetGuid(fixture.Cube);
		const char* ui = Nyx::Reflection::FindMetadataValue(Nyx::Reflection::GetTypeMetadata<SurfaceAnchor>().Properties[0], "UI");
		Require(ui && std::string_view(ui) == "EntityRef", "An EntityGuid property should get UI = EntityRef from the header tool");
		fixture.Frame();
		fixture.Frame();
		Require(fixture.GetHostItems().size() == 3, "The host field should show its eyedropper, select button and combo");

		fixture.OpenCombo();
		Require(fixture.GetListItems().size() == 4, "The combo should list its filter, None and both entities");
		// The filter has the keyboard once the list opens
		ImGui::GetIO().AddInputCharactersUTF8("CUB");
		fixture.Frame();
		fixture.Frame();
		Require(fixture.Editing.DetailsContext.EntityFilter == "CUB", "Typing should go into the list's filter");
		Require(fixture.GetListItems().size() == 3, "The filter should leave None and the cube, whatever the case");
		fixture.Press(fixture.GetListItems()[2].GetCenter());
		Require(fixture.GetHost() == cubeGuid, "Choosing the cube from the combo should make it the host");

		fixture.Editing.Undo();
		Require(!fixture.GetHost().IsValid(), "Undo should clear the host again");
		fixture.Editing.Redo();
		Require(fixture.GetHost() == cubeGuid, "Redo should make the cube the host again");

		fixture.Frame();
		fixture.OpenCombo();
		fixture.Press(fixture.GetListItems()[1].GetCenter());
		Require(!fixture.GetHost().IsValid(), "Choosing None should clear the host");
		fixture.Editing.Undo();
		Require(fixture.GetHost() == cubeGuid, "Undoing None should bring the host back");
	}

	// An entity dragged from the outliner onto the host field becomes the host, without being selected
	// on the way; a click on a row still selects it
	void TestEntityReferenceDropFromOutliner()
	{
		EntityFieldFixture fixture;
		fixture.Frame();
		fixture.Frame();
		fixture.DragTo(fixture.GetOutlinerRow(fixture.Cube).GetCenter(), fixture.GetHostItems().back().GetCenter());
		Require(fixture.GetHost() == fixture.Editing.Scene.GetGuid(fixture.Cube), "The cube dropped on the field should become the host");
		Require(fixture.Editing.Scene.GetSelection() == fixture.Decal, "Dragging the cube shouldn't select it");
		fixture.Editing.Undo();
		Require(!fixture.GetHost().IsValid(), "The drop should be one undo step");

		fixture.Press(fixture.GetOutlinerRow(fixture.Cube).GetCenter());
		Require(fixture.Editing.Scene.GetSelection() == fixture.Cube, "A click on a row should still select it");
	}

	// The eyedropper takes the entity a scene view picks; Esc, or the field going away, stops it. The
	// select button selects the host; a missing host shows as missing and can't be selected.
	void TestEntityReferencePickSelectAndMissing()
	{
		EntityFieldFixture fixture;
		const Registry& world = fixture.Editing.Scene.GetRegistry();
		const EntityGuid cubeGuid = fixture.Editing.Scene.GetGuid(fixture.Cube);
		EntityPickState& pick = fixture.Editing.DetailsContext.EntityPick;
		fixture.Frame();
		fixture.Frame();

		fixture.Press(fixture.GetHostItems()[0].GetCenter());
		Require(pick.IsWaiting(), "The eyedropper should wait for a click in a scene view");
		// What SceneViewPanel::ApplyPickResult() does with a click on the cube
		pick.Picked = cubeGuid;
		fixture.Frame();
		Require(fixture.GetHost() == cubeGuid && !pick.IsWaiting(), "The eyedropper should make the picked entity the host, and stop");
		fixture.Editing.Undo();
		Require(!fixture.GetHost().IsValid(), "A pick should be one undo step");

		fixture.Press(fixture.GetHostItems()[0].GetCenter());
		Require(pick.IsWaiting(), "The eyedropper should wait again");
		fixture.UI.Press(ImGuiKey_Escape, false, false, [&]()
			{
				fixture.Draw();
			});
		Require(!pick.IsWaiting(), "Esc should stop the eyedropper");

		fixture.Frame();
		fixture.Press(fixture.GetHostItems()[0].GetCenter());
		fixture.Editing.Scene.GetSelection() = fixture.Cube;
		fixture.Frame();
		Require(!pick.IsWaiting(), "Selecting another entity should stop the eyedropper");

		fixture.Editing.Scene.GetSelection() = fixture.Decal;
		fixture.Editing.Scene.GetRegistry().Get<SurfaceDecalComponent>(fixture.Decal).Anchor.Host = cubeGuid;
		fixture.Frame();
		fixture.Press(fixture.GetHostItems()[1].GetCenter());
		Require(fixture.Editing.Scene.GetSelection() == fixture.Cube, "The select button should select the host");

		Require(DescribeEntityReference(&world, cubeGuid) == "Cube", "A host should show its name");
		Require(DescribeEntityReference(&world, EntityGuid{}) == "None", "No host should show as None");
		const EntityGuid missing{ 0x1234 };
		Require(DescribeEntityReference(&world, missing) == "Missing (0x0000000000001234)",
			"A guid no entity has should show as missing, with the guid");

		fixture.Editing.Scene.GetSelection() = fixture.Decal;
		fixture.Editing.Scene.GetRegistry().Get<SurfaceDecalComponent>(fixture.Decal).Anchor.Host = missing;
		fixture.Frame();
		fixture.Press(fixture.GetHostItems()[1].GetCenter());
		Require(fixture.Editing.Scene.GetSelection() == fixture.Decal, "A missing host can't be selected");
	}

	// A host chosen in the field is saved and loaded as the guid it was, and saved again byte for byte
	void TestEntityReferenceSavesAndLoads()
	{
		EntityFieldFixture fixture;
		const EntityGuid cubeGuid = fixture.Editing.Scene.GetGuid(fixture.Cube);
		const EntityGuid decalGuid = fixture.Editing.Scene.GetGuid(fixture.Decal);
		fixture.Frame();
		fixture.Frame();
		fixture.OpenCombo();
		fixture.Press(fixture.GetListItems()[2].GetCenter());
		Require(fixture.GetHost() == cubeGuid, "The cube should be the host before saving");

		const Nyx::Tests::TestDirectory folder("NyxEntityReferenceTests");
		Require(SceneSerializer::SaveToFile(fixture.Editing.Scene.GetRegistry(), folder.Path / "Saved.nyxscene"), "The scene should save");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(folder.Path / "Saved.nyxscene", loaded), "The scene should load");
		const std::optional<Entity> decal = FindEntityByGuid(loaded, decalGuid);
		Require(decal && loaded.Get<SurfaceDecalComponent>(*decal).Anchor.Host == cubeGuid, "The loaded decal should have the cube as its host");
		Require(FindEntityByGuid(loaded, cubeGuid).has_value(), "The loaded scene should have the cube under the same guid");

		Require(SceneSerializer::SaveToFile(loaded, folder.Path / "Again.nyxscene"), "The loaded scene should save");
		const auto read = [](const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		};
		Require(read(folder.Path / "Saved.nyxscene") == read(folder.Path / "Again.nyxscene"), "Saving the loaded scene should give the same file");
	}

	void TestCrashCards()
	{
		HeadlessUI ui;
		CrashCardsPanel crashCards;

		std::optional<GameCrash> restart;
		ui.Frame([&]()
			{
				restart = crashCards.Draw();
			});
		Require(!restart && crashCards.IsEmpty(), "Without crashes there should be no cards");

		GameCrash crash;
		crash.GameName = "Game 2";
		crash.ExitCode = 0xC0000005;
		crashCards.Add(crash, "last line");
		ui.Frame([&]()
			{
				restart = crashCards.Draw();
				Require(ImGui::FindWindowByName("Game Crashed") != nullptr, "A crash should show the Game Crashed window");
			});
		Require(!restart && !crashCards.IsEmpty(), "A card should stay until dismissed or restarted");
	}

	void TestScenePopupsAndIdleSession()
	{
		HeadlessUI ui;
		SceneFilePopups popups;
		const AssetTypeRegistry types;
		AssetDatabase assets{ types };

		popups.RequestSaveAs();
		SceneFileRequest request;
		ui.Frame([&]()
			{
				request = popups.Draw(assets);
				Require(ImGui::IsPopupOpen("Save Scene As"), "RequestSaveAs should open the Save Scene As popup");
			});
		Require(!request.Load && !request.SaveAs, "Opening a popup should not request anything");

		// Play's commands do nothing harmful without games
		Nyx::Editor::SceneDocument scene;
		GameLinkPanel gameLink;
		PlaySession session(scene, gameLink);
		session.Stop();
		session.OnSceneReplaced();
		session.Update();
		Require(!session.IsRunning() && !session.IsQuitting() && session.TakeCrashes().empty(), "An idle session should stay idle");
		session.SetPaused(true);
		session.Step();
		Require(!session.IsPaused() && !session.CanPause() && !session.CanStep(), "Pause and Step should do nothing without games");
	}

	// The painted widgets (UIWidgets.h) behave as ImGui's own: mouse, keyboard, disabled, and the
	// last-item queries.
	void TestPaintedWidgetsBehaveLikeImGuis()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		int clicks = 0;
		bool bToggle = false;
		float gain = 0.25f;
		bool bButtonHovered = false;
		bool bButtonActive = false;
		bool bFocusButton = false;
		bool bDisabled = false;
		ImRect button;
		ImRect toggle;
		ImRect slider;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
				ImGui::Begin("Painted widgets");
				if (bFocusButton)
				{
					// As Tab or an arrow key would: focus, and show the keyboard's cursor, without
					// which ImGui doesn't activate by keyboard
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNavCursorVisible(true);
				}
				ImGui::BeginDisabled(bDisabled);
				if (Nyx::UI::Button("Apply"))
				{
					++clicks;
				}
				ImGui::EndDisabled();
				bButtonHovered = ImGui::IsItemHovered();
				bButtonActive = ImGui::IsItemActive();
				button = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				Nyx::UI::Toggle("Snap", &bToggle);
				toggle = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				ImGui::SetNextItemWidth(200.0f);
				Nyx::UI::SliderFloat("Gain", &gain, 0.0f, 1.0f);
				// The slider's frame: the item without its label
				slider = ImRect(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + 200.0f, ImGui::GetItemRectMax().y));
				ImGui::End();
			};
		const auto click = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};

		// Lay out once, then hover the button, press and release it
		ui.Frame(draw);
		io.AddMousePosEvent(button.GetCenter().x, button.GetCenter().y);
		ui.Frame(draw);
		Require(bButtonHovered && !bButtonActive, "IsItemHovered() should be true over a painted button");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		Require(bButtonActive && clicks == 0, "A held painted button should be active, and not clicked before it is released");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(clicks == 1, "Releasing over a painted button should click it");

		click(toggle.GetCenter());
		Require(bToggle, "Clicking a toggle should switch it on");

		// The slider's left end is its minimum; its right end, past the track, its maximum.
		click(ImVec2(slider.Min.x + 1.0f, slider.GetCenter().y));
		Require(gain == 0.0f, "Clicking a slider's left end should set its minimum, got " + std::to_string(gain));
		click(ImVec2(slider.Max.x - 1.0f, slider.GetCenter().y));
		Require(gain == 1.0f, "Clicking a slider's right end should set its maximum, got " + std::to_string(gain));

		// The keyboard: focus the button, then Space clicks it.
		io.AddMousePosEvent(-100.0f, -100.0f);
		bFocusButton = true;
		ui.Frame(draw);
		bFocusButton = false;
		ui.Frame(draw);
		io.AddKeyEvent(ImGuiKey_Space, true);
		ui.Frame(draw);
		io.AddKeyEvent(ImGuiKey_Space, false);
		ui.Frame(draw);
		Require(clicks == 2, "Space should click a painted button with keyboard focus, clicks " + std::to_string(clicks));

		// Disabled, it ignores the mouse.
		bDisabled = true;
		click(button.GetCenter());
		Require(clicks == 2, "A disabled painted button shouldn't click");
	}

	// Nyx::UI::TreeRow() behaves as ImGui's tree nodes do: the row selects, the arrow, a double
	// click and the Left and Right keys open and close, and children are indented.
	void TestTreeRowsBehaveLikeTreeNodes()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		int clicks = 0;
		bool bOpen = false;
		bool bHovered = false;
		bool bFocus = false;
		ImRect row;
		float rowContentX = 0.0f;
		float childContentX = 0.0f;
		float workMinX = 0.0f;
		float workMaxX = 0.0f;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
				ImGui::Begin("Tree rows");
				workMinX = ImGui::GetCurrentWindow()->WorkRect.Min.x;
				workMaxX = ImGui::GetCurrentWindow()->WorkRect.Max.x;
				if (bFocus)
				{
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNavCursorVisible(true);
				}
				Nyx::UI::TreeRowOptions options;
				options.Icon = Nyx::UI::ETreeRowIcon::Folder;
				rowContentX = ImGui::GetCursorScreenPos().x;
				const Nyx::UI::TreeRowResult result = Nyx::UI::TreeRow("Assets", options);
				clicks += result.bClicked ? 1 : 0;
				bOpen = result.bOpen;
				bHovered = ImGui::IsItemHovered();
				row = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				if (result.bOpen)
				{
					childContentX = ImGui::GetCursorScreenPos().x;
					Nyx::UI::TreeRowOptions leaf;
					leaf.bLeaf = true;
					Nyx::UI::TreeRow("Meshes", leaf);
					Nyx::UI::TreeRowEnd();
				}
				ImGui::End();
			};
		const auto press = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		const auto pressKey = [&](ImGuiKey key)
			{
				io.AddKeyEvent(key, true);
				ui.Frame(draw);
				io.AddKeyEvent(key, false);
				ui.Frame(draw);
			};

		ui.Frame(draw);
		Require(row.Min.x >= workMinX && row.Max.x <= workMaxX, "A tree row should stay inside its window");

		// Anywhere on the row but its arrow selects it; the arrow is where the row's content starts.
		const ImVec2 middle = row.GetCenter();
		const ImVec2 farRight(ImLerp(row.Min.x, row.Max.x, 0.9f), row.GetCenter().y);
		const ImVec2 arrow(rowContentX + 1.0f, row.GetCenter().y);
		io.AddMousePosEvent(middle.x, middle.y);
		ui.Frame(draw);
		Require(bHovered, "IsItemHovered() should be true over a tree row");

		press(middle);
		Require(clicks == 1 && !bOpen, "Clicking a row should select it, and not open it");

		press(arrow);
		Require(clicks == 1 && bOpen, "Clicking a row's arrow should open it, and not select it");
		Require(childContentX > rowContentX, "An open row's children should be indented");
		press(arrow);
		Require(!bOpen, "Clicking the arrow again should close the row");

		press(farRight);
		Require(clicks == 2 && !bOpen, "A click far right on the row should select it too: the row spans its window");

		// Two clicks in a row: a double click
		press(middle);
		press(middle);
		Require(bOpen && clicks == 4, "A double click on the row should open it, and select it");

		// The keyboard: Left closes the row with focus, Right opens it.
		io.AddMousePosEvent(-100.0f, -100.0f);
		bFocus = true;
		ui.Frame(draw);
		bFocus = false;
		ui.Frame(draw);
		pressKey(ImGuiKey_LeftArrow);
		Require(!bOpen, "Left should close an open row with keyboard focus");
		pressKey(ImGuiKey_RightArrow);
		Require(bOpen, "Right should open a closed row with keyboard focus");
	}

	// Fields are ImGui's own over a painted frame: ImGui keeps dragging and typing, and the frame
	// is painted under ImGui's. Property rows lay out a label column and a value column, which
	// starts at the same place at any indentation, and stay inside their window. Collapsing
	// headers keep their open state where ImGui's do.
	void TestFieldsAndPropertyRows()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiContext& g = *GImGui;

		float number = 1.0f;
		float vector[3] = { 0.0f, 0.0f, 0.0f };
		std::string text = "Cube";
		bool bNumberActivated = false;
		bool bNumberCommitted = false;
		bool bVectorCommitted = false;
		bool bLabelHovered = false;
		bool bFramePainted = false;
		bool bStyleRestored = false;
		bool bImGuiHeader = false;
		bool bHeaderOpen = false;
		ImRect header;
		ImRect label;
		ImRect numberField;
		ImRect vectorField;
		ImRect textField;
		float workMinX = 0.0f;
		float workMaxX = 0.0f;
		float firstRowTop = 0.0f;
		float secondRowTop = 0.0f;
		float nestedValueX = 0.0f;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
				ImGui::Begin("Fields");
				workMinX = ImGui::GetCurrentWindow()->WorkRect.Min.x;
				workMaxX = ImGui::GetCurrentWindow()->WorkRect.Max.x;

				bHeaderOpen = bImGuiHeader ? ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) : Nyx::UI::CollapsingHeader("Transform");
				header = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				firstRowTop = ImGui::GetCursorScreenPos().y;
				Nyx::UI::BeginPropertyRow("Intensity");
				bLabelHovered = ImGui::IsItemHovered();
				label = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				const int colors = g.ColorStack.Size;
				const int styleVars = g.StyleVarStack.Size;
				Nyx::UI::DragFloat("##Number", &number, 0.5f);
				bNumberActivated |= ImGui::IsItemActivated();
				bNumberCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				numberField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				bStyleRestored = g.ColorStack.Size == colors && g.StyleVarStack.Size == styleVars;
				Nyx::UI::EndPropertyRow();

				secondRowTop = ImGui::GetCursorScreenPos().y;
				Nyx::UI::BeginPropertyRow("Position");
				Nyx::UI::DragVector("##Vector", vector, 3, 1.0f);
				bVectorCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				vectorField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::EndPropertyRow();

				Nyx::UI::BeginPropertyRow("Name");
				Nyx::UI::InputText("##Text", &text);
				textField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::EndPropertyRow();

				Nyx::UI::TreeRowOptions group;
				group.bDefaultOpen = true;
				group.bToggleOnClick = true;
				if (Nyx::UI::TreeRow("Lighting", group).bOpen)
				{
					Nyx::UI::BeginPropertyRow("Shadows");
					nestedValueX = ImGui::GetCursorScreenPos().x;
					ImGui::TextUnformatted("On");
					Nyx::UI::EndPropertyRow();
					Nyx::UI::TreeRowEnd();
				}

				// The number field's painted frame: a filled rect inside ImGui's frame, under its middle
				bFramePainted = false;
				for (const PainterCommand& command : Nyx::UI::GetWindowPainter().GetPainter().GetCommands())
				{
					const PainterRect rect = GetPayload<PainterRectPayload>(command).Rect;
					const ImRect painted(ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, rect.Max.y));
					bFramePainted |= command.Type == static_cast<uint32_t>(EPainterCommandType::Rect) && command.OutlineWidth == 0.0f &&
						numberField.Contains(painted) && painted.Contains(numberField.GetCenter());
				}
				ImGui::End();
			};
		const auto moveTo = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
			};
		const auto press = [&](ImVec2 at)
			{
				moveTo(at);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		const auto pressKey = [&](ImGuiKey key)
			{
				io.AddKeyEvent(key, true);
				ui.Frame(draw);
				io.AddKeyEvent(key, false);
				ui.Frame(draw);
			};

		ui.Frame(draw);
		ui.Frame(draw);
		const auto isInside = [&](const ImRect& field)
			{
				return field.Min.x >= workMinX && field.Max.x <= workMaxX;
			};
		Require(isInside(numberField) && isInside(vectorField) && isInside(textField), "A property row's value should stay inside its window");
		Require(label.Max.x <= numberField.Min.x && numberField.Min.y >= firstRowTop, "A property row's value should be right of its label, in its row");
		Require(secondRowTop >= numberField.Max.y && vectorField.Min.y >= numberField.Max.y && textField.Min.y >= vectorField.Max.y,
			"Property rows should follow each other without overlapping");
		Require(vectorField.Min.x == numberField.Min.x && vectorField.Max.x == numberField.Max.x, "A vector field's components should share the value column");
		Require(nestedValueX == numberField.Min.x, "A property row inside a tree row should keep the value column where it is");
		Require(bFramePainted, "A number field's frame should be painted under it");
		Require(bStyleRestored, "A field should give ImGui's style back as it found it");

		moveTo(label.GetCenter());
		Require(bLabelHovered, "IsItemHovered() after BeginPropertyRow() should be true over the label");

		// Dragging the number to the right raises it, and records one edit when let go.
		moveTo(numberField.GetCenter());
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		Require(bNumberActivated, "Pressing a number field should activate it");
		for (int step = 1; step <= 4; ++step)
		{
			moveTo(ImVec2(numberField.GetCenter().x + 5.0f * static_cast<float>(step), numberField.GetCenter().y));
		}
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(number > 1.0f && bNumberCommitted, "Dragging a number field should raise it and commit the edit, got " + std::to_string(number));

		// A double click types into it instead.
		press(numberField.GetCenter());
		press(numberField.GetCenter());
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		pressKey(ImGuiKey_A);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("42");
		ui.Frame(draw);
		pressKey(ImGuiKey_Enter);
		Require(number == 42.0f, "Typing into a number field after a double click should set it, got " + std::to_string(number));

		// Dragging the vector's middle component changes only Y.
		const ImVec2 y = vectorField.GetCenter();
		moveTo(y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		for (int step = 1; step <= 4; ++step)
		{
			moveTo(ImVec2(y.x + 5.0f * static_cast<float>(step), y.y));
		}
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(vector[0] == 0.0f && vector[1] > 0.0f && vector[2] == 0.0f, "Dragging a vector's Y should change only Y");
		Require(bVectorCommitted, "IsItemDeactivatedAfterEdit() after a vector field should cover its components");

		// Typing at the end of the text field
		press(textField.GetCenter());
		pressKey(ImGuiKey_End);
		io.AddInputCharactersUTF8("s");
		ui.Frame(draw);
		pressKey(ImGuiKey_Enter);
		Require(text == "Cubes", "Typing into a text field should change its text, got " + text);

		// The header: open at first, closed by a click, and ImGui's own header under the same
		// label finds it closed.
		Require(bHeaderOpen, "A collapsing header should be open at first");
		press(header.GetCenter());
		Require(!bHeaderOpen, "Clicking a collapsing header should close it");
		bImGuiHeader = true;
		ui.Frame(draw);
		Require(!bHeaderOpen, "ImGui's collapsing header under the same label should find it closed");
	}

	// Checkboxes flip on a click. Menu items, selectables and combos keep ImGui's behaviour (a click
	// closes the popup, a combo opens its list and takes the item chosen), while ImGui draws nothing
	// of them. A colour swatch opens a picker, and a drag in it is one edit.
	void TestChecksMenusCombosAndSwatches()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiContext& g = *GImGui;

		bool bChecked = false;
		bool bCheckClicked = false;
		bool bOpenMenu = false;
		bool bMenuOpen = false;
		bool bTicked = false;
		int copies = 0;
		int pastes = 0;
		bool bImGuiDrewNothing = true;
		int comboIndex = 0;
		bool bComboChanged = false;
		bool bComboOpen = false;
		glm::vec3 color(0.5f);
		Nyx::UI::ColorSwatchResult swatch;
		bool bEditStarted = false;
		bool bEditEnded = false;
		ImRect checkbox;
		ImRect copy;
		ImRect paste;
		ImRect tick;
		ImRect combo;
		ImVec2 secondItemAt;
		ImRect swatchRect;
		bool bPickerOpen = false;
		ImVec2 pickerTopLeft;
		const char* const items[] = { "First", "Second", "Third" };
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));
				ImGui::Begin("Widgets");
				bCheckClicked |= Nyx::UI::Checkbox("Snap", &bChecked);
				checkbox = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				ImGui::SetNextItemWidth(200.0f);
				bComboChanged |= Nyx::UI::Combo("Editor", &comboIndex, items);
				combo = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				bComboOpen = ImGui::IsPopupOpen(ImHashStr("##ComboPopup", 0, ImGui::GetItemID()), ImGuiPopupFlags_None);
				if (bComboOpen && g.OpenPopupStack.back().Window != nullptr)
				{
					// The middle of the list's second item: items are a line high, the item
					// spacing apart
					const ImGuiWindow& list = *g.OpenPopupStack.back().Window;
					const float line = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
					secondItemAt = ImVec2(list.DC.CursorStartPos.x + 20.0f, list.DC.CursorStartPos.y + line + ImGui::GetFontSize() * 0.5f);
				}

				ImGui::SetNextItemWidth(200.0f);
				swatch = Nyx::UI::ColorSwatch("Colour", &color.x);
				bEditStarted |= swatch.bEditStarted;
				bEditEnded |= swatch.bEditEnded;
				swatchRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				bPickerOpen = ImGui::IsPopupOpen(ImHashStr("##ColorPopup", 0, ImGui::GetItemID()), ImGuiPopupFlags_None);
				if (bPickerOpen && g.OpenPopupStack.back().Window != nullptr)
				{
					pickerTopLeft = g.OpenPopupStack.back().Window->DC.CursorStartPos;
				}

				if (bOpenMenu)
				{
					ImGui::OpenPopup("Menu");
					bOpenMenu = false;
				}
				bMenuOpen = ImGui::BeginPopup("Menu");
				if (bMenuOpen)
				{
					ImDrawList& drawList = *ImGui::GetWindowDrawList();
					const int vertices = drawList.VtxBuffer.Size;
					copies += Nyx::UI::MenuItem("Copy", "Ctrl+C") ? 1 : 0;
					copy = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					pastes += Nyx::UI::MenuItem("Paste", "Ctrl+V", false, false) ? 1 : 0;
					paste = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					Nyx::UI::Separator();
					Nyx::UI::MenuItem("Snap to grid", nullptr, &bTicked);
					tick = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					bImGuiDrewNothing &= drawList.VtxBuffer.Size == vertices;
					ImGui::EndPopup();
				}
				ImGui::End();
			};
		const auto moveTo = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
			};
		const auto press = [&](ImVec2 at)
			{
				moveTo(at);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		const auto openMenu = [&]()
			{
				bOpenMenu = true;
				ui.Frame(draw);
				ui.Frame(draw);
			};

		ui.Frame(draw);
		press(checkbox.GetCenter());
		Require(bChecked && bCheckClicked, "Clicking a checkbox should tick it");

		// A menu: a click on an item runs it and closes the menu; a disabled one does nothing.
		openMenu();
		Require(bMenuOpen, "The menu should open");
		Require(bImGuiDrewNothing, "ImGui should draw nothing of painted menu items and separators");
		press(paste.GetCenter());
		Require(pastes == 0 && bMenuOpen, "A disabled menu item shouldn't run, nor close the menu");
		press(copy.GetCenter());
		// The menu was open when the frame began; it is closed the next.
		ui.Frame(draw);
		Require(copies == 1 && !bMenuOpen, "Clicking a menu item should run it and close the menu");
		openMenu();
		press(tick.GetCenter());
		Require(bTicked, "A menu item with a bool should flip it");

		// A combo: a click opens its list; a click on an item chooses it and closes the list.
		press(combo.GetCenter());
		Require(bComboOpen, "Clicking a combo should open its list");
		// A popup is placed in the frames after it opens.
		ui.Frame(draw);
		ui.Frame(draw);
		press(secondItemAt);
		Require(comboIndex == 1 && bComboChanged && !bComboOpen, "Clicking an item of the list should choose it and close the list");

		// A colour swatch: a click opens the picker; a drag in its square changes the colour and
		// is one edit.
		press(swatchRect.GetCenter());
		Require(bPickerOpen && !bEditStarted, "Clicking a swatch should open its picker, without starting an edit");
		ui.Frame(draw);
		ui.Frame(draw);
		const ImVec2 square(pickerTopLeft.x + 20.0f, pickerTopLeft.y + 20.0f);
		moveTo(square);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		moveTo(ImVec2(square.x + 30.0f, square.y + 10.0f));
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		ui.Frame(draw);
		Require(color != glm::vec3(0.5f), "Dragging in the picker should change the colour");
		Require(bEditStarted && bEditEnded, "A drag in the picker should start and end an edit");
	}

	// The filled rects a window painter recorded across every column of a table, below its top: its
	// header strip, stripes and row-wide selection pills
	std::vector<PainterRect> RowWideRects(const Nyx::UI::WindowPainter& painter, float tableTop, float firstColumnRight, float lastColumnLeft)
	{
		std::vector<PainterRect> rects;
		for (const PainterCommand& command : painter.GetPainter().GetCommands())
		{
			const PainterRect rect = GetPayload<PainterRectPayload>(command).Rect;
			if (command.Type == static_cast<uint32_t>(EPainterCommandType::Rect) && command.OutlineWidth == 0.0f && rect.Min.y >= tableTop - 0.5f &&
				rect.Min.x < firstColumnRight && rect.Max.x > lastColumnLeft)
			{
				rects.push_back(rect);
			}
		}
		return rects;
	}

	// A table's rows as ImGui laid them out: each row's index and top
	struct TableRowTop
	{
		int Index = 0;
		float Top = 0.0f;
	};

	// Which of `rows` holds `y`, and the row's bottom; the last row is as high as the others. Null
	// when none does.
	const TableRowTop* FindTableRow(const std::vector<TableRowTop>& rows, float y, float& outBottom)
	{
		for (size_t index = 0; index < rows.size(); ++index)
		{
			const float bottom = index + 1 < rows.size() ? rows[index + 1].Top : rows[index].Top + (rows[1].Top - rows[0].Top);
			if (y >= rows[index].Top && y < bottom)
			{
				outBottom = bottom;
				return &rows[index];
			}
		}
		return nullptr;
	}

	// Asset cards select on a click and open on a double click. Painted tables paint a header
	// strip with separators, a stripe on every other row (also rows a clipper ends), a selected
	// row across all its columns, and a line on a column border being hovered; each inside the
	// rows ImGui laid out.
	void TestAssetCardsAndTables()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();

		int clicks = 0;
		int opens = 0;
		ImRect card;
		bool bClipped = false;
		int selectedRow = -1;
		float workMinX = 0.0f;
		float firstColumnRight = 0.0f;
		ImRect tableRect;
		std::vector<TableRowTop> rows;
		std::vector<PainterRect> rowRects;
		int separators = 0;
		bool bBorderLine = false;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(500.0f, 500.0f));
				ImGui::Begin("Cards and tables");
				Nyx::UI::AssetCardOptions options;
				const Nyx::UI::AssetCardResult result = Nyx::UI::AssetCard("Cube.nyxmesh", options);
				clicks += result.bClicked ? 1 : 0;
				opens += result.bOpened ? 1 : 0;
				card = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				// Four rows under a header, or a hundred in a scrolling table with a clipper
				const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
					(bClipped ? ImGuiTableFlags_ScrollY : ImGuiTableFlags_None);
				if (Nyx::UI::BeginTable("Table", 3, flags, glm::vec2(400.0f, bClipped ? 200.0f : 0.0f)))
				{
					const ImGuiTable& table = *ImGui::GetCurrentTable();
					workMinX = table.WorkRect.Min.x;
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Time");
					ImGui::TableSetupColumn("Game");
					ImGui::TableSetupColumn("Text");
					Nyx::UI::TableHeadersRow();
					rows.clear();
					ImGuiListClipper clipper;
					clipper.Begin(bClipped ? 100 : 4);
					while (Nyx::UI::TableClipperStep(clipper))
					{
						for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
						{
							Nyx::UI::TableNextRow();
							rows.push_back(TableRowTop{ row, table.RowPosY1 });
							ImGui::PushID(row);
							ImGui::TableNextColumn();
							if (Nyx::UI::Selectable("12:00", row == selectedRow, ImGuiSelectableFlags_SpanAllColumns))
							{
								selectedRow = row;
							}
							ImGui::TableNextColumn();
							ImGui::TextUnformatted("Game 1");
							ImGui::TableNextColumn();
							ImGui::TextUnformatted("Linked");
							ImGui::PopID();
						}
					}
					firstColumnRight = table.Columns[0].MaxX;
					tableRect = table.OuterRect;
					const Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
					rowRects = RowWideRects(painter, tableRect.Min.y, firstColumnRight, table.Columns[2].MinX);
					separators = 0;
					for (const PainterCommand& command : painter.GetPainter().GetCommands())
					{
						separators += command.Type == static_cast<uint32_t>(EPainterCommandType::Line) ? 1 : 0;
					}
					Nyx::UI::EndTable();
				}

				// The border line, painted after the table into the window: upright on the border,
				// reaching below the header, where the header's separators end
				bBorderLine = false;
				for (const PainterCommand& command : Nyx::UI::GetWindowPainter().GetPainter().GetCommands())
				{
					const auto line = GetPayload<PainterLinePayload>(command);
					bBorderLine |= command.Type == static_cast<uint32_t>(EPainterCommandType::Line) && line.From.x == line.To.x &&
						std::abs(line.From.x - firstColumnRight) <= 1.0f && !rows.empty() && std::max(line.From.y, line.To.y) > rows[0].Top;
				}
				ImGui::End();
			};
		const auto press = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		// The row-wide rects below the header row, and the rows they lie in; each must lie in one
		const auto findStriped = [&](std::vector<int>& outRows)
			{
				outRows.clear();
				for (const PainterRect& rect : rowRects)
				{
					if (rect.Min.y < rows.front().Top - 0.5f)
					{
						continue;
					}
					float bottom = 0.0f;
					const TableRowTop* row = FindTableRow(rows, (rect.Min.y + rect.Max.y) * 0.5f, bottom);
					Require(row != nullptr && rect.Min.y >= row->Top - 0.5f && rect.Max.y <= bottom + 0.5f, "A row's painting should lie inside its row");
					outRows.push_back(row->Index);
				}
			};

		ui.Frame(draw);
		ui.Frame(draw);
		press(card.GetCenter());
		Require(clicks == 1 && opens == 0, "A click on an asset card should select it, not open it");
		press(card.GetCenter());
		Require(opens == 1, "A double click on an asset card should open it");

		// The header strip, inside the header row; then a stripe on the second and fourth rows
		io.AddMousePosEvent(-100.0f, -100.0f);
		ui.Frame(draw);
		Require(rows.size() == 4, "The table should lay out four rows");
		Require(!rowRects.empty() && rowRects[0].Max.y <= rows[0].Top + 0.5f, "A table should paint its header strip above its rows");
		std::vector<int> striped;
		findStriped(striped);
		Require(striped == std::vector<int>{ 1, 3 }, "A table should stripe every other row, the second and the fourth");
		Require(separators == 2, "Three header cells should have two separators between them, got " + std::to_string(separators));

		// A row selected by its selectable gets a pill across all columns.
		press(ImVec2((workMinX + firstColumnRight) * 0.5f, (rows[0].Top + rows[1].Top) * 0.5f));
		io.AddMousePosEvent(-100.0f, -100.0f);
		for (int frame = 0; frame < 30; ++frame)
		{
			ui.Frame(draw);
		}
		findStriped(striped);
		Require(selectedRow == 0 && std::count(striped.begin(), striped.end(), 0) == 1, "A selected row should be painted across the table");

		// A hovered column border shows as a line, after ImGui's short delay.
		io.AddMousePosEvent(firstColumnRight, (rows[1].Top + rows[2].Top) * 0.5f);
		for (int frame = 0; frame < 10; ++frame)
		{
			ui.Frame(draw);
		}
		Require(bBorderLine, "A hovered column border should show as a line");

		// With a clipper in a scrolling table: every other row striped, the rows the clipper ends
		// included
		io.AddMousePosEvent(-100.0f, -100.0f);
		selectedRow = -1;
		bClipped = true;
		// Until the selection's pill has faded
		for (int frame = 0; frame < 30; ++frame)
		{
			ui.Frame(draw);
		}
		findStriped(striped);
		Require(striped.size() > 2, "A scrolling table should stripe the rows it shows");
		for (size_t index = 1; index < striped.size(); ++index)
		{
			Require(striped[index] % 2 == 1 && striped[index] == striped[index - 1] + 2,
				"Stripes in a clipped table should be on every other row, row " + std::to_string(striped[index]));
		}
	}

	// The window frame's bar widgets: clicking a bar tab selects it; a document tab's close button
	// takes a click from the tab around it, and is disabled without a way to close; an icon button
	// clicks unless disabled; a status item is hovered under the mouse, e.g. for a tooltip.
	void TestBarWidgets()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		const ImGuiContext& g = *GImGui;

		bool bCanClose = true;
		bool bBuildDisabled = false;
		int selectedTab = 0;
		int tabClicks = 0;
		int closeClicks = 0;
		int buildClicks = 0;
		bool bStatusHovered = false;
		ImRect tab;
		ImGuiID closeId = 0;
		ImRect barTabs[2];
		ImRect build;
		ImRect status;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
				ImGui::Begin("Bars");
				const Nyx::UI::DocumentTabResult result = Nyx::UI::DocumentTab("TestScene", bCanClose, glm::vec2(0.0f, 34.0f));
				tab = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				closeId = ImHashStr("##Close", 0, ImGui::GetItemID());
				tabClicks += result.bClicked ? 1 : 0;
				closeClicks += result.bCloseClicked ? 1 : 0;
				const char* const names[] = { "Scene", "Materials" };
				for (int index = 0; index < 2; ++index)
				{
					if (index > 0)
					{
						ImGui::SameLine();
					}
					if (Nyx::UI::BarTab(names[index], index == selectedTab, glm::vec2(0.0f, 32.0f)))
					{
						selectedTab = index;
					}
					barTabs[index] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				}
				ImGui::BeginDisabled(bBuildDisabled);
				buildClicks += Nyx::UI::IconButton("Build", Nyx::UI::EIcon::Build, glm::vec2(0.0f, 29.0f), Nyx::UI::EButtonTone::Primary, true) ? 1 : 0;
				ImGui::EndDisabled();
				build = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::StatusItem("1 error", Nyx::UI::EStatusIcon::Error, 33.0f);
				bStatusHovered = ImGui::IsItemHovered();
				status = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				ImGui::End();
			};
		const auto click = [&](ImVec2 position)
			{
				io.AddMousePosEvent(position.x, position.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};

		io.AddMousePosEvent(-100.0f, -100.0f);
		ui.Frame(draw);
		ui.Frame(draw);

		// Clicking a bar tab selects it
		click(barTabs[1].GetCenter());
		Require(selectedTab == 1, "Clicking a bar tab should select it");
		click(barTabs[0].GetCenter());
		Require(selectedTab == 0, "Clicking the other bar tab should select that one");

		// The close button: hovered along the tab's middle, from its right end, until ImGui has the
		// button's ID hovered; its left edge is where that stops.
		const float middleY = tab.GetCenter().y;
		float closeRight = 0.0f;
		float closeLeft = 0.0f;
		for (float x = tab.Max.x - 1.0f; x > tab.Min.x; x -= 1.0f)
		{
			io.AddMousePosEvent(x, middleY);
			ui.Frame(draw);
			const bool bOverClose = g.HoveredId == closeId;
			if (bOverClose && closeRight == 0.0f)
			{
				closeRight = x;
			}
			if (!bOverClose && closeRight != 0.0f)
			{
				closeLeft = x + 1.0f;
				break;
			}
		}
		Require(closeRight != 0.0f && closeLeft != 0.0f, "A document tab should have a close button inside it");
		const ImVec2 closeCentre((closeLeft + closeRight) * 0.5f, middleY);
		const ImVec2 tabOnly((tab.Min.x + closeLeft) * 0.5f, middleY);

		// The close button takes the click; the rest of the tab gets its own
		click(closeCentre);
		Require(closeClicks == 1 && tabClicks == 0, "Clicking a document tab's close button should close it, and not click the tab");
		click(tabOnly);
		Require(closeClicks == 1 && tabClicks == 1, "Clicking a document tab left of its close button should click the tab");
		bCanClose = false;
		click(closeCentre);
		Require(closeClicks == 1 && tabClicks == 1, "A disabled close button should take no click, nor pass it to the tab");

		click(build.GetCenter());
		Require(buildClicks == 1, "Clicking an icon button should click it");
		bBuildDisabled = true;
		click(build.GetCenter());
		Require(buildClicks == 1, "A disabled icon button should take no click");

		io.AddMousePosEvent(status.GetCenter().x, status.GetCenter().y);
		ui.Frame(draw);
		Require(bStatusHovered, "IsItemHovered() should be true over a status item");
	}

	// Which theme file a preference names, and the preference a file gets: the default theme is
	// no preference, a theme in Editor/Themes its name, one elsewhere its path. The themes folder
	// holds the default theme.
	void TestThemePreferences()
	{
		using Nyx::Editor::ThemeEditorWindow;
		const std::filesystem::path folder = ThemeEditorWindow::GetProjectThemesFolder();
		const std::filesystem::path defaultFile = ThemeEditorWindow::GetDefaultThemeFile(folder);

		Require(ThemeEditorWindow::ResolveThemeFile("", folder) == defaultFile, "No theme preference should be the default theme");
		Require(ThemeEditorWindow::ResolveThemeFile("Light.nyxtheme", folder) == folder / "Light.nyxtheme", "A theme's name should be a file in the themes folder");
		const std::filesystem::path elsewhere = std::filesystem::temp_directory_path() / "Elsewhere.nyxtheme";
		Require(ThemeEditorWindow::ResolveThemeFile(elsewhere.string(), folder) == elsewhere, "A whole path should stay as it is");

		Require(ThemeEditorWindow::GetThemePreference(defaultFile, folder).empty(), "The default theme should need no preference");
		Require(ThemeEditorWindow::GetThemePreference(folder / "Light.nyxtheme", folder) == "Light.nyxtheme", "A theme in the themes folder should be remembered by name");
		Require(ThemeEditorWindow::GetThemePreference(elsewhere, folder) == elsewhere.string(), "A theme elsewhere should be remembered by its path");
		for (const std::filesystem::path& file : { defaultFile, folder / "Light.nyxtheme", elsewhere })
		{
			Require(ThemeEditorWindow::ResolveThemeFile(ThemeEditorWindow::GetThemePreference(file, folder), folder) == file,
				"A theme's preference should name it again: " + file.string());
		}

		const std::vector<std::filesystem::path> files = ThemeEditorWindow::FindThemeFiles(folder);
		Require(std::any_of(files.begin(), files.end(), [&](const std::filesystem::path& file) { return file.filename() == defaultFile.filename(); }),
			"The themes folder should list the default theme");
		Require(std::is_sorted(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) { return a.filename().string() < b.filename().string(); }),
			"The themes should be listed by name");
	}

	// Choosing a theme in the theme window, in a themes folder of the test's own: without unsaved
	// changes it loads in the next frame and the preferences name it; with unsaved changes the
	// window asks first and keeps them, Escape cancels and Enter loads. The question is the
	// window's own modal popup (it once opened in the combo's ID scope, where nothing drew it).
	void TestThemeChoice()
	{
		using Nyx::Editor::ThemeEditorWindow;
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "NyxThemeChoiceTest";
		std::error_code ignored;
		std::filesystem::remove_all(folder, ignored);
		Nyx::UI::Theme orange;
		orange.Accent = *Nyx::UI::ParseThemeColor("#FF8800");
		const std::filesystem::path defaultFile = ThemeEditorWindow::GetDefaultThemeFile(folder);
		Require(!Nyx::UI::SaveThemeFile(defaultFile, Nyx::UI::Theme{}) && !Nyx::UI::SaveThemeFile(folder / "Orange.nyxtheme", orange),
			"Writing the test's themes failed");
		const glm::vec4 green(0.0f, 1.0f, 0.0f, 1.0f);

		{
			HeadlessUI ui;
			ImGuiIO& io = ImGui::GetIO();
			ThemeEditorWindow window(folder);
			Nyx::Editor::EditorPreferences preferences;
			window.LoadAtStartup(preferences);
			window.bOpen = true;
			const auto draw = [&]()
				{
					window.Draw(preferences);
				};
			const auto pressKey = [&](ImGuiKey key)
				{
					io.AddKeyEvent(key, true);
					ui.Frame(draw);
					io.AddKeyEvent(key, false);
					ui.Frame(draw);
				};
			const auto isAsking = [&]()
				{
					const ImGuiWindow* popup = ImGui::FindWindowByName("Discard Theme Changes?");
					return window.IsAskingToDiscard() && popup != nullptr && popup->Active;
				};
			ui.Frame(draw);
			Require(window.GetActiveFile() == defaultFile && preferences.Theme.empty(), "Without a preference, the default theme should load");

			window.ChooseTheme(folder / "Orange.nyxtheme");
			ui.Frame(draw);
			Require(!window.IsAskingToDiscard() && Nyx::UI::GetTheme().Accent == orange.Accent && preferences.Theme == "Orange.nyxtheme",
				"Without unsaved changes, a theme chosen should load at once and be remembered by name");

			Nyx::UI::GetTheme().Accent = green;
			window.ChooseTheme(defaultFile);
			ui.Frame(draw);
			ui.Frame(draw);
			Require(isAsking() && Nyx::UI::GetTheme().Accent == green && window.GetActiveFile() == folder / "Orange.nyxtheme",
				"With unsaved changes, the window should ask before loading, and keep them meanwhile");
			pressKey(ImGuiKey_Escape);
			Require(!window.IsAskingToDiscard() && Nyx::UI::GetTheme().Accent == green && preferences.Theme == "Orange.nyxtheme",
				"Escape should keep the unsaved changes and the theme loaded");

			window.ChooseTheme(defaultFile);
			ui.Frame(draw);
			ui.Frame(draw);
			Require(isAsking(), "Choosing again should ask again");
			pressKey(ImGuiKey_Enter);
			Require(!window.IsAskingToDiscard() && Nyx::UI::FormatThemeColor(Nyx::UI::GetTheme().Accent) == Nyx::UI::FormatThemeColor(Nyx::UI::Theme{}.Accent),
				"Enter should load the theme chosen, dropping the unsaved changes");
			Require(window.GetActiveFile() == defaultFile && preferences.Theme.empty(), "The default theme should need no preference again");
		}

		// The other tests expect the default theme
		Nyx::UI::GetTheme() = Nyx::UI::Theme{};
		std::filesystem::remove_all(folder, ignored);
	}

	// The titlebar's widgets: a button in a tone of its own and a menu bar button click, a window
	// control is hovered under the mouse and clicks, a multi-line field lies on a painted frame,
	// and a logarithmic slider's knob sits where ImGui's grab is.
	void TestTitlebarWidgetsAndMultilineFields()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();

		bool bMenuOpen = false;
		int playClicks = 0;
		int menuClicks = 0;
		int closeClicks = 0;
		bool bCloseHovered = false;
		float zoom = 100.0f;
		std::string log = "first line\nsecond line";
		ImRect play;
		ImRect menu;
		ImRect close;
		ImRect details;
		ImRect slider;
		std::vector<PainterCommand> commands;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
				ImGui::Begin("Titlebar");
				playClicks += Nyx::UI::Button("Play", glm::vec2(64.0f, 28.0f), Nyx::UI::EButtonTone::Go) ? 1 : 0;
				play = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				menuClicks += Nyx::UI::MenuBarButton("File", glm::vec2(56.0f, 28.0f), bMenuOpen) ? 1 : 0;
				menu = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				closeClicks += Nyx::UI::WindowControlButton("##Close", Nyx::UI::EWindowControl::Close, glm::vec2(46.0f, 32.0f)) ? 1 : 0;
				bCloseHovered = ImGui::IsItemHovered();
				close = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::InputTextMultiline("##Log", &log, glm::vec2(-FLT_MIN, 80.0f), ImGuiInputTextFlags_ReadOnly);
				details = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				ImGui::SetNextItemWidth(200.0f);
				Nyx::UI::SliderFloat("Zoom", &zoom, 10.0f, 1000.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic);
				slider = ImRect(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + 200.0f, ImGui::GetItemRectMax().y));
				const std::span<const PainterCommand> painted = Nyx::UI::GetWindowPainter().GetPainter().GetCommands();
				commands.assign(painted.begin(), painted.end());
				ImGui::End();
			};
		const auto settle = [&]()
			{
				for (int frame = 0; frame < 40; ++frame)
				{
					ui.Frame(draw);
				}
			};
		const auto click = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};

		io.AddMousePosEvent(-100.0f, -100.0f);
		settle();
		// The field's painted frame: a filled rect inside ImGui's, under its middle
		Require(std::any_of(commands.begin(), commands.end(), [&](const PainterCommand& command)
					{
						const PainterRect rect = GetPayload<PainterRectPayload>(command).Rect;
						const ImRect painted(ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, rect.Max.y));
						return command.Type == static_cast<uint32_t>(EPainterCommandType::Rect) && command.OutlineWidth == 0.0f && details.Contains(painted) &&
							painted.Contains(details.GetCenter());
					}),
			"A multi-line field's frame should be painted under it");

		click(play.GetCenter());
		Require(playClicks == 1, "Clicking a toned button should click it");
		click(menu.GetCenter());
		Require(menuClicks == 1, "Clicking a menu bar button should click it, to open its menu");
		bMenuOpen = true;
		click(menu.GetCenter());
		Require(menuClicks == 2, "A menu bar button whose menu is open should still click");

		Require(!bCloseHovered, "A window control shouldn't be hovered with the mouse elsewhere");
		io.AddMousePosEvent(close.GetCenter().x, close.GetCenter().y);
		ui.Frame(draw);
		Require(bCloseHovered, "IsItemHovered() should be true over a window control");
		click(close.GetCenter());
		Require(closeClicks == 1, "Clicking a window control should click it");

		// A read-only field keeps its text whatever is typed.
		io.AddMousePosEvent(details.GetCenter().x, details.GetCenter().y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		io.AddInputCharactersUTF8("x");
		ui.Frame(draw);
		Require(log == "first line\nsecond line", "A read-only multi-line field should keep its text, got " + log);

		// The logarithmic slider: the middle of the track is the range's geometric middle, and
		// the painted knob sits there.
		io.AddMousePosEvent(-100.0f, -100.0f);
		ui.Frame(draw);
		zoom = 100.0f;
		settle();
		const auto knob = std::find_if(commands.begin(), commands.end(), [&](const PainterCommand& command)
			{
				const glm::vec2 centre = GetPayload<PainterCirclePayload>(command).Center;
				return command.Type == static_cast<uint32_t>(EPainterCommandType::Circle) && centre.y > slider.Min.y && centre.y < slider.Max.y;
			});
		Require(knob != commands.end(), "A slider should paint its knob");
		const float knobX = GetPayload<PainterCirclePayload>(*knob).Center.x;
		io.AddMousePosEvent(knobX, slider.GetCenter().y);
		ui.Frame(draw);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(std::abs(zoom - 100.0f) < 5.0f, "Clicking a logarithmic slider's knob should keep its value, as ImGui's grab is there, got " + std::to_string(zoom));
	}

	// With the chrome on, the editor's panels leave no shapes to ImGui: what Debug > Mark ImGui
	// Shapes finds in them is nothing. ImGui's text stays, and so do its close and collapse
	// buttons, which it draws in the text colour; the text is made transparent here, which hides
	// them from the marks.
	void TestPanelsLeaveNoShapesToImGui()
	{
		HeadlessUI ui;
		RegisterDefaultPropertyWidgets();
		Nyx::UI::SetPaintedChrome(true);
		ImGuiStyle& style = ImGui::GetStyle();
		const ImVec4 text = style.Colors[ImGuiCol_Text];
		style.Colors[ImGuiCol_Text].w = 0.0f;

		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});
		editing.Scene.GetSelection() = cube;

		SceneOutlinerPanel outliner;
		DetailsPanel details;
		CrashCardsPanel crashCards;
		GameCrash crash;
		crash.GameName = "Game 1";
		crash.ExitCode = 0xC0000005;
		crash.Report = Nyx::CrashReport{ 0xC0000005, "access violation", std::vector<Nyx::CrashStackFrame>(3) };
		crashCards.Add(crash, "last line");
		Nyx::Editor::SceneDocument scene;
		GameLinkPanel gameLink;
		PlaySession session(scene, gameLink);
		EditorPreferences preferences;
		PlayControls play;
		play.bShowSetups = true;
		bool bGameLinkOpen = true;

		// Side by side, so each shows
		const auto place = [](float x)
			{
				ImGui::SetNextWindowPos(ImVec2(x, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(240.0f, 700.0f));
			};
		Nyx::UI::SetMarkImGuiShapes(true);
		for (int frame = 0; frame < 3; ++frame)
		{
			ImGui::NewFrame();
			place(0.0f);
			outliner.Draw(editing);
			place(250.0f);
			details.Draw(editing);
			place(500.0f);
			crashCards.Draw();
			place(750.0f);
			gameLink.Draw(bGameLinkOpen);
			place(1000.0f);
			play.DrawSetupsWindow(preferences);
			ImGui::EndFrame();
			Nyx::UI::MarkImGuiShapes();
			ImGui::Render();
		}
		const uint32_t shapes = Nyx::UI::GetImGuiShapeCount();

		// Window > Input, every tab, with a keyboard, a mouse and a pad that reports all it has
		Nyx::Input::VirtualInputBackend backend;
		Nyx::Input::InputSystem input;
		input.AddBackend(backend);
		backend.AddDevice(Nyx::Input::EDeviceKind::Keyboard, "Keyboard", 1000);
		backend.AddDevice(Nyx::Input::EDeviceKind::Mouse, "Mouse", 1000);
		const Nyx::Input::InputDeviceId pad = backend.AddDevice(Nyx::Input::EDeviceKind::Gamepad, "Pad", 1000);
		Nyx::Input::InputDeviceInfo padInfo;
		padInfo.Name = "Pad";
		padInfo.Model = Nyx::Input::EGamepadModel::DualSense;
		padInfo.Capabilities = Nyx::Input::EDeviceCapability::Gyro | Nyx::Input::EDeviceCapability::Touchpad | Nyx::Input::EDeviceCapability::Rumble;
		backend.SetDeviceInfo(pad, padInfo, 1000);
		// With an input asset, for the Tuning and Checks tabs: a stick with its curve, a tap with its
		// timeline, the contexts as an array, the checks
		const std::filesystem::path inputAssetPath = std::filesystem::temp_directory_path() / "NyxEditorPanelTests.nyxinput";
		std::filesystem::remove(inputAssetPath);
		InputAssetDocument inputAsset;
		Require(inputAsset.Load(input, inputAssetPath), "The input asset should be made and read");
		inputAsset.Attach(editing.Transactions);
		Nyx::Input::AddRecipe(inputAsset.GetAsset(), 0, Nyx::Input::EInputRecipe::TapAndHold, "Use");
		inputAsset.Apply();
		EditorCameraController cameras(input);
		InputPanel inputPanel;
		inputPanel.bOpen = true;
		inputPanel.Attach(inputAsset, editing.Transactions);

		// And a recording of it, replayed with its controls in the last pass
		const std::filesystem::path recordingPath = std::filesystem::temp_directory_path() / "NyxEditorPanelTests.nyxinputrec";
		Nyx::Input::InputRecordingSession recording;
		Require(recording.Start(input, recordingPath, "NyxEditor"), "The input should be recorded");

		const int debugTabs = static_cast<int>(Nyx::Input::InputDebugView::ETab::Count);
		uint32_t inputShapes = 0;
		uint64_t time = 1000;
		for (int tab = 0; tab < debugTabs + 5; ++tab)
		{
			if (tab == debugTabs + 4)
			{
				recording.Stop();
				Require(inputPanel.OpenRecording(recordingPath), "The recording should replay");
				inputPanel.SelectTab(Nyx::Input::InputDebugView::ETab::Timeline);

				// Through A and B, the Timeline drawing both
				inputAsset.KeepAsA();
				inputAsset.GetAsset().Contexts[0].Bindings[1].Pipeline.DeadZone.Inner = 0.5f;
				inputAsset.Apply();
				inputPanel.SetComparing(true);
				Require(inputPanel.GetPlaybackB() != nullptr, "The recording should replay through B too");
			}
			else if (tab < debugTabs)
			{
				inputPanel.SelectTab(static_cast<Nyx::Input::InputDebugView::ETab>(tab));
			}
			else
			{
				inputPanel.SelectExtraTab(tab == debugTabs + 3 ? "Checks" : "Tuning");
				if (tab == debugTabs)
				{
					inputPanel.GetTuning().SelectBinding(0, 4);
				}
				else if (tab == debugTabs + 1)
				{
					inputPanel.GetTuning().SelectBinding(0, inputAsset.GetAsset().Contexts[0].Bindings.size() - 4);
				}
				else if (tab == debugTabs + 2)
				{
					inputPanel.GetTuning().SelectBinding(99, 0);
				}
			}
			for (int frame = 0; frame < 3; ++frame)
			{
				time += 16000;
				backend.SetValue(pad, Nyx::Input::Gamepad(Nyx::Input::EGamepadControl::LeftStick), glm::vec3(0.3f, 0.1f * static_cast<float>(frame), 0.0f), time - 1);
				backend.SetValue(pad, Nyx::Input::Gamepad(Nyx::Input::EGamepadControl::Touchpad0), glm::vec3(0.5f, 0.5f, 1.0f), time - 1);
				input.BeginFrame(time);
				recording.Update();
				inputPanel.Record(input);

				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(1200.0f, 780.0f));
				inputPanel.Draw(input);
				ImGui::EndFrame();
				Nyx::UI::MarkImGuiShapes();
				ImGui::Render();
				inputShapes += Nyx::UI::GetImGuiShapeCount();

				input.EndFrame();
			}
		}

		Nyx::UI::SetMarkImGuiShapes(false);
		style.Colors[ImGuiCol_Text] = text;
		Nyx::UI::SetPaintedChrome(false);
		std::filesystem::remove(inputAssetPath);
		Require(inputPanel.GetPlayback() && inputPanel.GetPlayback()->GetFrame() > 0, "The recording should have played");
		std::filesystem::remove(recordingPath);
		Require(shapes == 0, "The editor's panels should leave no shapes to ImGui, found " + std::to_string(shapes));
		Require(inputShapes == 0, "The Input window should leave no shapes to ImGui, found " + std::to_string(inputShapes));
	}

	void TestAnimationsEaseAndAreForgotten()
	{
		HeadlessUI ui;
		const ImGuiID id = 0x1234;
		constexpr float Speed = 18.0f;
		float value = -1.0f;

		ui.Frame([&]() { value = Nyx::UI::Animate(id, 0.0f, Speed); });
		Require(value == 0.0f, "An animation should start at its target");

		// One frame of 1/60 s towards 1: 1 - e^(-18 / 60)
		ui.Frame([&]()
			{
				value = Nyx::UI::Animate(id, 1.0f, Speed);
				Require(Nyx::UI::Animate(id, 1.0f, Speed) == value, "An animation should move once per frame, however often it is asked");
			});
		const float expected = 1.0f - std::exp(-Speed / 60.0f);
		Require(std::abs(value - expected) < 1e-4f, "One frame should ease " + std::to_string(expected) + " of the way, got " + std::to_string(value));

		for (int frame = 0; frame < 60; ++frame)
		{
			ui.Frame([&]() { value = Nyx::UI::Animate(id, 1.0f, Speed); });
		}
		Require(value == 1.0f, "An animation should arrive at its target");

		const size_t kept = Nyx::UI::GetAnimationCount();
		for (int frame = 0; frame <= Nyx::UI::AnimationForgetFrames + 1; ++frame)
		{
			ui.Frame([]() {});
		}
		Require(kept == 1 && Nyx::UI::GetAnimationCount() == 0, "An animation not asked for in a while should be forgotten");
	}

	// Zooming the Painter Test window bakes glyphs at new sizes, which can make ImGui's font atlas
	// grow into a new texture in the middle of a frame. The ImDrawList comparison then still held
	// glyphs on the old texture, and handing that to a draw list asserted.
	void TestPainterComparisonSurvivesAtlasGrowth()
	{
		// A font atlas that grows as glyphs are baked, as the editor's does
		ImGuiContext* context = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280, 800);
		io.DeltaTime = 1.0f / 60.0f;
		ImFont* font = io.Fonts->AddFontDefault();
		ImGui::NewFrame();

		Painter painter;
		painter.BeginFrame(400, 200);
		painter.AddText(font, 13.0f, { 10.0f, 10.0f }, glm::vec4(1.0f), "Before");
		const ImTextureData* oldTexture = io.Fonts->TexData;

		// Large glyphs until the atlas moves to a bigger texture
		for (float size = 100.0f; size < 400.0f && io.Fonts->TexData == oldTexture; size += 20.0f)
		{
			for (ImWchar character = 'A'; character <= 'Z'; ++character)
			{
				font->GetFontBaked(size)->FindGlyph(character);
			}
		}
		Require(io.Fonts->TexData != oldTexture, "The font atlas should have grown");

		painter.AddText(font, 13.0f, { 10.0f, 40.0f }, glm::vec4(1.0f), "After");
		painter.EndFrame();
		Require(painter.GetTextures().size() == 2, "Text before and after the growth should use the old and the new texture");

		ImGui::Begin("Comparison");
		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		// The window's own title is drawn on the new texture too; only what comes after counts.
		const unsigned int indicesBefore = static_cast<unsigned int>(drawList.IdxBuffer.Size);
		DrawPainterFrameWithImDrawList(painter, drawList, ImGui::GetCursorScreenPos());
		ImGui::End();

		bool bUsesOldTexture = false;
		bool bUsesNewTexture = false;
		for (const ImDrawCmd& command : drawList.CmdBuffer)
		{
			bUsesOldTexture |= command.TexRef._TexData == oldTexture;
			bUsesNewTexture |= command.TexRef._TexData == io.Fonts->TexData && command.ElemCount > 0 && command.IdxOffset >= indicesBefore;
		}
		Require(!bUsesOldTexture, "The comparison should leave out glyphs on the replaced texture");
		Require(bUsesNewTexture, "The comparison should draw glyphs on the new texture");

		ImGui::EndFrame();
		ImGui::DestroyContext(context);
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();

		TestCreateAndDeleteAreUndoable();
		TestSelectionFollowsGuid();
		TestDuplicateCopyAndPaste();
		TestForgetScene();
		TestHotkeys();
		TestPanelsDrawHeadless();
		TestPanelsDockNarrow();
		TestRuntimeMeshDetails();
		TestGroundFollowsEdits();
		TestScatterFollowsBlockers();
		TestDetailsHaveNoConflictingIds();
		TestEntityReferenceCombo();
		TestEntityReferenceDropFromOutliner();
		TestEntityReferencePickSelectAndMissing();
		TestEntityReferenceSavesAndLoads();
		TestCrashCards();
		TestScenePopupsAndIdleSession();
		TestPainterComparisonSurvivesAtlasGrowth();
		TestPaintedWidgetsBehaveLikeImGuis();
		TestTreeRowsBehaveLikeTreeNodes();
		TestFieldsAndPropertyRows();
		TestChecksMenusCombosAndSwatches();
		TestAssetCardsAndTables();
		TestTitlebarWidgetsAndMultilineFields();
		TestBarWidgets();
		TestThemePreferences();
		TestThemeChoice();
		TestPanelsLeaveNoShapesToImGui();
		TestAnimationsEaseAndAreForgotten();
		// Last: it gives the camera a post-load step of its own
		TestPostLoadStepsKnowTheirEntity();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor panel test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor panel tests passed.\n";
	return 0;
}

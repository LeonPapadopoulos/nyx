// The editor's edits, undo and redo become live edit messages, and a second world (the game's)
// that applies them ends up like the editor's after every step.
#include "AssetReference.h"
#include "CameraComponent.h"
#include "ComponentEdits.h"
#include "ComponentPostLoadSubscriber.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "IAssetResolver.h"
#include "InspectorDrawContext.h"
#include "EditorLinkMessages.h"
#include "EntityCopies.h"
#include "GameLinkSubscriber.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "SceneEntityTransactionDomain.h"
#include "SceneSerializer.h"
#include "TransactionDiffUtil.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransformComponent.h"

#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using namespace Nyx::Engine;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	std::map<uint64_t, std::vector<std::byte>> GetEntitiesByGuid(const Registry& world)
	{
		std::map<uint64_t, std::vector<std::byte>> entities;
		world.ForEachEntity([&](Entity entity)
			{
				BinaryWriter writer;
				Require(SceneSerializer::WriteEntity(world, entity, writer), "An entity can't be written");
				Require(entities.emplace(world.Get<GuidComponent>(entity).Guid.Value, writer.GetBytes()).second,
					"Two entities have the same guid");
			});
		return entities;
	}

	// Hands out a made-up mesh and material per path, and remembers which paths were asked for;
	// the tests have no renderer to load real ones
	class TestAssetResolver final : public IAssetResolver
	{
	public:
		Nyx::Mesh* ResolveMesh(const std::string& meshId) override
		{
			MeshRequests.push_back(meshId);
			return reinterpret_cast<Nyx::Mesh*>(Fake(meshId));
		}

		Nyx::Material* ResolveMaterial(const std::string& materialId) override
		{
			MaterialRequests.push_back(materialId);
			return reinterpret_cast<Nyx::Material*>(Fake(materialId));
		}

		// A distinct, never dereferenced address per path
		std::byte* Fake(const std::string& path)
		{
			std::vector<std::byte>& storage = Storage[path];
			storage.resize(1);
			return storage.data();
		}

		std::vector<std::string> MeshRequests;
		std::vector<std::string> MaterialRequests;
		std::map<std::string, std::vector<std::byte>> Storage;
	};

	// Where a MeshRenderer's mesh (an AssetReference) is, as the details panel records it
	SubobjectPath MeshLocation()
	{
		const Nyx::Reflection::TypeMetadata& meshRendererType = Nyx::Reflection::GetTypeMetadata<MeshRendererComponent>();
		const std::optional<size_t> meshIndex = Nyx::Reflection::FindPropertyIndexByName(meshRendererType, "Mesh");
		Require(meshIndex.has_value(), "MeshRenderer has no property Mesh");
		return SubobjectPath{ .SubobjectType = &meshRendererType, .PropertyIndices = { *meshIndex } };
	}

	// The editor's transaction system as EditorLayer sets it up, and the game's world
	struct EditorAndGame
	{
		Nyx::SceneDocument Scene;
		SceneEntityTransactionDomain Domain;
		TransactionSystem Transactions;
		EditorTransactionContext Context;
		GameLinkSubscriber GameEdits{ Scene };
		TestAssetResolver Assets;
		ComponentPostLoadSubscriber AssetLoader{ Scene };

		Registry Game;
		std::vector<EEditorLinkMessage> Sent;

		EditorAndGame()
		{
			Transactions.RegisterDomain(EObjectDomain::SceneEntity, &Domain);
			Transactions.Subscribe(&AssetLoader);
			Transactions.Subscribe(&GameEdits);
			Context.ActiveScene = &Scene;
			AssetLoader.SetPostLoadContext(ScenePostLoadContext{ .AssetResolver = &Assets });
		}

		// Play: the game runs the scene as it is now, and edits from here on are kept for it
		void Play()
		{
			Game.Clear();
			const Registry& world = Scene.GetRegistry();
			world.ForEachEntity([&](Entity entity)
				{
					const std::optional<CreateEntityMessage> message = MakeCreateEntityMessage(world, entity);
					Require(message && ApplyLiveEdit(MakeNetMessage(*message), Game, {}) == ELiveEditResult::Applied, "Play failed");
				});
			GameEdits.StartSession();
		}

		// The editor's next frame with the game linked: the edits go to the game, which applies them
		void SendEdits(const std::string& step)
		{
			for (const Nyx::Net::Message& message : GameEdits.TakeMessages())
			{
				Sent.push_back(static_cast<EEditorLinkMessage>(message.Type));
				Require(ApplyLiveEdit(message, Game, {}) == ELiveEditResult::Applied, step + ": the game couldn't apply a message");
			}

			Require(GetEntitiesByGuid(Scene.GetRegistry()) == GetEntitiesByGuid(Game), step + ": the game's world differs from the editor's");
		}

		std::vector<EEditorLinkMessage> TakeSent()
		{
			return std::exchange(Sent, {});
		}

		// Like the gizmo and the details panel: snapshot, change, commit. For a struct inside a
		// component, location says where it is, as the details panel records it.
		template <typename TObject, typename TChange>
		void Edit(Entity entity, TObject& object, const char* label, TChange change, const SubobjectPath& location = {})
		{
			TransactionDiffUtil diff;
			diff.TakeSnapshot(MakeSceneEntityRef(Scene, entity), &object, Nyx::Reflection::GetTypeMetadata<TObject>(), location);
			change(object);
			Require(diff.CommitChanges(label, Transactions), std::string(label) + " recorded nothing");
		}

		// Like the outliner's Add Entity button
		Entity AddEntity()
		{
			const Entity entity = Scene.CreateEntity("New Entity");
			const ObjectRef target = MakeSceneEntityRef(Scene, entity);

			Transaction transaction;
			transaction.Label = "Create Entity";
			transaction.Changes.push_back(Change{ EChangeKind::AddObject,
				AddObjectChange{ .Target = target, .AfterCreate = CaptureRootObjectSnapshot(Domain, Context, target) } });
			Transactions.Push(std::move(transaction));
			return entity;
		}

		// Like the outliner's Delete Selected button
		void DeleteEntity(Entity entity)
		{
			const ObjectRef target = MakeSceneEntityRef(Scene, entity);
			RootObjectSnapshot snapshot = CaptureRootObjectSnapshot(Domain, Context, target);
			Require(Scene.DestroyEntity(entity), "Deleting failed");

			Transaction transaction;
			transaction.Label = "Delete Entity";
			transaction.Changes.push_back(Change{ EChangeKind::DeleteObject, DeleteObjectChange{ .Target = target, .BeforeDelete = snapshot } });
			Transactions.Push(std::move(transaction));
		}
	};

	void TestEditsUndoAndRedo()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();

		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<TransformComponent>(cube, TransformComponent{});
		auto& meshRenderer = world.Add<MeshRendererComponent>(cube, MeshRendererComponent{});
		meshRenderer.Mesh = AssetReference{ "Mesh", "Cube" };

		const Entity camera = test.Scene.CreateEntity("Camera");
		world.Add<TransformComponent>(camera, TransformComponent{});
		world.Add<CameraComponent>(camera, CameraComponent{});

		// Before Play, nothing is kept
		test.Edit(cube, world.Get<TransformComponent>(cube), "Translate Entity",
			[](TransformComponent& transform) { transform.Position.x = 1.0f; });
		Require(test.GameEdits.TakeMessages().empty(), "An edit before Play was kept");

		test.Play();
		test.SendEdits("Play");

		// Moving the cube, and undo and redo
		test.Edit(cube, world.Get<TransformComponent>(cube), "Translate Entity",
			[](TransformComponent& transform) { transform.Position = glm::vec3(2.0f, 3.0f, 4.0f); });
		test.SendEdits("Move");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::SetProperties }, "A move isn't one SetProperties");

		Require(test.Transactions.Undo(test.Context), "Undo failed");
		test.SendEdits("Undo the move");
		Require(world.Get<TransformComponent>(cube).Position.x == 1.0f, "Undo didn't move the cube back");

		Require(test.Transactions.Redo(test.Context), "Redo failed");
		test.SendEdits("Redo the move");

		// The camera's field of view
		test.Edit(camera, world.Get<CameraComponent>(camera), "Edit Property",
			[](CameraComponent& cameraComponent) { cameraComponent.FovYDegrees = 80.0f; });
		test.SendEdits("Field of view");

		// A field of a struct inside a component, as the details panel records it
		test.Edit(cube, world.Get<MeshRendererComponent>(cube).Mesh, "Edit Property",
			[](AssetReference& mesh) { mesh.Path = "Sphere"; }, MeshLocation());
		test.SendEdits("Mesh path");
		Require(test.TakeSent().back() == EEditorLinkMessage::SetProperties, "A struct field edit wasn't sent");

		// Undo and redo change the struct field back and forth, and the game follows
		Require(test.Transactions.Undo(test.Context), "Undo of the mesh path failed");
		Require(world.Get<MeshRendererComponent>(cube).Mesh.Path == "Cube", "Undo didn't change the mesh path back");
		test.SendEdits("Undo the mesh path");
		Require(test.Transactions.Redo(test.Context), "Redo of the mesh path failed");
		Require(world.Get<MeshRendererComponent>(cube).Mesh.Path == "Sphere", "Redo didn't change the mesh path again");
		test.SendEdits("Redo the mesh path");
		Require(test.Transactions.Undo(test.Context), "Second undo of the mesh path failed");
		test.SendEdits("Undo the mesh path again");

		// Several edits while the game is still starting arrive together, in order
		test.Edit(cube, world.Get<TransformComponent>(cube), "Scale Entity",
			[](TransformComponent& transform) { transform.Scale = glm::vec3(2.0f); });
		test.Edit(cube, world.Get<TransformComponent>(cube), "Translate Entity",
			[](TransformComponent& transform) { transform.Position.y = -1.0f; });
		test.Transactions.Undo(test.Context);
		test.SendEdits("Edits made together");
	}

	void TestAddingAndDeletingEntities()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity floor = test.Scene.CreateEntity("Floor");
		world.Add<TransformComponent>(floor, TransformComponent{});

		test.Play();
		test.SendEdits("Play");

		const Entity added = test.AddEntity();
		test.SendEdits("Add");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::CreateEntity }, "Adding isn't one CreateEntity");

		test.Edit(added, world.Get<NameComponent>(added), "Edit Property", [](NameComponent& name) { name.Name = "Lamp"; });
		test.SendEdits("Rename");

		test.DeleteEntity(added);
		test.SendEdits("Delete");
		Require(test.TakeSent().back() == EEditorLinkMessage::DeleteEntity, "Deleting isn't sent as DeleteEntity");

		// Undo brings it back with its name and guid; undo again removes it
		Require(test.Transactions.Undo(test.Context), "Undo of delete failed");
		test.SendEdits("Undo the delete");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::CreateEntity }, "Undoing a delete isn't sent as CreateEntity");

		test.Transactions.Undo(test.Context);
		test.SendEdits("Undo the rename");

		test.Transactions.Undo(test.Context);
		test.SendEdits("Undo the add");

		test.Transactions.Redo(test.Context);
		test.SendEdits("Redo the add");

		test.Transactions.Redo(test.Context);
		test.Transactions.Redo(test.Context);
		test.SendEdits("Redo the rename and the delete");

		// The floor, deleted and brought back
		test.DeleteEntity(floor);
		test.Transactions.Undo(test.Context);
		test.SendEdits("Delete and undo in one frame");
	}

	// The entity alive with this guid, if any
	std::optional<Entity> FindByGuid(const Registry& world, EntityGuid guid)
	{
		std::optional<Entity> found;
		world.ForEachEntity([&](Entity entity)
			{
				if (world.Has<GuidComponent>(entity) && world.Get<GuidComponent>(entity).Guid == guid)
				{
					Require(!found, "Two entities have the same guid");
					found = entity;
				}
			});
		return found;
	}

	// Delete A, add B into A's slot, undo twice: A has to come back, although its handle now
	// belongs to B. Undo steps name entities by guid for this.
	void TestUndoAfterSlotReuse()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity a = test.Scene.CreateEntity("A");
		world.Add<TransformComponent>(a, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });
		const EntityGuid guidA = world.Get<GuidComponent>(a).Guid;

		test.Play();
		test.SendEdits("Play");

		test.DeleteEntity(a);
		test.SendEdits("Delete A");

		const Entity b = test.AddEntity();
		Require(b.Index() == a.Index(), "B should take A's slot, or this test doesn't test slot reuse");
		const EntityGuid guidB = world.Get<GuidComponent>(b).Guid;
		test.SendEdits("Add B");

		Require(test.Transactions.Undo(test.Context), "Undo of the add failed");
		test.SendEdits("Undo the add of B");
		Require(!FindByGuid(world, guidB), "B is still there after undoing its add");

		Require(test.Transactions.Undo(test.Context), "Undo of the delete failed");
		const std::optional<Entity> restoredA = FindByGuid(world, guidA);
		Require(restoredA.has_value(), "A didn't come back after undoing its delete");
		Require(world.Get<NameComponent>(*restoredA).Name == "A", "A came back without its name");
		Require(world.Has<TransformComponent>(*restoredA) && world.Get<TransformComponent>(*restoredA).Position == glm::vec3(1.0f, 2.0f, 3.0f),
			"A came back without its transform");
		test.SendEdits("Undo the delete of A");

		// Redo both: A is gone and B is back, with its own guid
		Require(test.Transactions.Redo(test.Context) && test.Transactions.Redo(test.Context), "Redo failed");
		Require(!FindByGuid(world, guidA) && FindByGuid(world, guidB), "Redo didn't delete A and add B again");
		test.SendEdits("Redo the delete and the add");

		// An edit of B recorded now still finds B after it moves through undo and redo again
		test.Edit(*FindByGuid(world, guidB), world.Get<NameComponent>(*FindByGuid(world, guidB)), "Edit Property",
			[](NameComponent& name) { name.Name = "B"; });
		Require(test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context),
			"Undoing rename, add and delete failed");
		Require(FindByGuid(world, guidA) && !FindByGuid(world, guidB), "Undoing everything should leave only A");
		test.SendEdits("Undo everything");

		Require(test.Transactions.Redo(test.Context) && test.Transactions.Redo(test.Context) && test.Transactions.Redo(test.Context),
			"Redoing everything failed");
		const std::optional<Entity> finalB = FindByGuid(world, guidB);
		Require(finalB && world.Get<NameComponent>(*finalB).Name == "B" && !FindByGuid(world, guidA),
			"Redoing everything should leave only B, renamed");
		test.SendEdits("Redo everything");
	}

	// Delete a cube with a mesh and material, undo, redo, undo: it comes back whole each time,
	// struct properties (AssetReference) included
	void TestUndoKeepsWholeComponents()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<TransformComponent>(cube, TransformComponent{ .Position = glm::vec3(4.0f, 5.0f, 6.0f) });
		world.Add<MeshRendererComponent>(cube, MeshRendererComponent{
			.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" },
			.Material = AssetReference{ .Type = "Material", .Path = "Materials/Textured.nyxmat" },
			.bVisible = true });
		const EntityGuid guid = world.Get<GuidComponent>(cube).Guid;

		test.Play();
		test.SendEdits("Play");

		const auto requireWhole = [&](const std::string& step)
		{
			const std::optional<Entity> entity = FindByGuid(world, guid);
			Require(entity.has_value(), step + ": the cube isn't there");
			Require(world.Has<MeshRendererComponent>(*entity), step + ": the cube has no MeshRenderer");
			const MeshRendererComponent& meshRenderer = world.Get<MeshRendererComponent>(*entity);
			Require(meshRenderer.Mesh.Type == "Mesh" && meshRenderer.Mesh.Path == "Meshes/Cube.nyxmesh", step + ": the mesh was lost");
			Require(meshRenderer.Material.Type == "Material" && meshRenderer.Material.Path == "Materials/Textured.nyxmat",
				step + ": the material was lost");
			Require(meshRenderer.bVisible, step + ": the cube became invisible");
			Require(world.Get<TransformComponent>(*entity).Position == glm::vec3(4.0f, 5.0f, 6.0f), step + ": the transform was lost");
		};

		test.DeleteEntity(cube);
		test.SendEdits("Delete");

		Require(test.Transactions.Undo(test.Context), "Undo of the delete failed");
		requireWhole("Undo the delete");
		test.SendEdits("Undo the delete");

		Require(test.Transactions.Redo(test.Context) && !FindByGuid(world, guid), "Redo of the delete failed");
		test.SendEdits("Redo the delete");

		Require(test.Transactions.Undo(test.Context), "Second undo of the delete failed");
		requireWhole("Undo the delete again");
		test.SendEdits("Undo the delete again");

		// The editor shows the cube again: its mesh and material are loaded, not only their paths
		const MeshRendererComponent& meshRenderer = world.Get<MeshRendererComponent>(*FindByGuid(world, guid));
		Require(meshRenderer.MeshAsset == reinterpret_cast<Nyx::Mesh*>(test.Assets.Fake("Meshes/Cube.nyxmesh")) &&
					meshRenderer.MaterialAsset == reinterpret_cast<Nyx::Material*>(test.Assets.Fake("Materials/Textured.nyxmat")),
			"Undoing the delete didn't load the cube's mesh and material");
	}

	// Typing a mesh path in the details panel loads that mesh in the editor, as the game does
	void TestEditsLoadAssets()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<MeshRendererComponent>(cube, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "" } });

		test.Play();
		test.SendEdits("Play");

		test.Edit(cube, world.Get<MeshRendererComponent>(cube).Mesh, "Edit Property",
			[](AssetReference& mesh) { mesh.Path = "Meshes/Sphere.nyxmesh"; }, MeshLocation());
		test.SendEdits("Type a mesh path");

		Require(world.Get<MeshRendererComponent>(cube).MeshAsset == reinterpret_cast<Nyx::Mesh*>(test.Assets.Fake("Meshes/Sphere.nyxmesh")),
			"A typed mesh path didn't load the mesh in the editor");

		// Any edit of the entity loads its assets again, e.g. moving it, which is harmless
		const size_t requestsBefore = test.Assets.MeshRequests.size();
		test.Edit(cube, world.Get<NameComponent>(cube), "Edit Property", [](NameComponent& name) { name.Name = "Ball"; });
		Require(test.Assets.MeshRequests.size() == requestsBefore + 1, "An edit should load the entity's assets once");

		// Undoing the rename and the typed path: the path is empty again, and so is the loaded mesh
		Require(test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context), "Undo failed");
		Require(world.Get<MeshRendererComponent>(cube).Mesh.Path.empty(), "Undo didn't clear the typed mesh path");
		Require(world.Get<MeshRendererComponent>(cube).MeshAsset == nullptr, "Undo kept the mesh of the typed path");
		test.SendEdits("Undo the typed path");
	}

	// While a value is dragged, the game gets it every frame it changes; the drag's end records one
	// undo step. Dragged away and back, nothing is recorded, but the game ends up like the editor.
	void TestValuesWhileDragging()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<TransformComponent>(cube, TransformComponent{});
		TransformComponent& transform = world.Get<TransformComponent>(cube);
		const auto& transformType = Nyx::Reflection::GetTypeMetadata<TransformComponent>();

		test.Play();
		test.SendEdits("Play");
		test.TakeSent();

		// As the gizmo does: snapshot at the start of the drag, a preview each frame, commit at the end
		TransactionDiffUtil drag;
		drag.TakeSnapshot(MakeSceneEntityRef(test.Scene, cube), &transform, transformType);

		transform.Position.x = 1.0f;
		Require(drag.PreviewChanges("Drag Entity", test.Transactions), "A changed value wasn't previewed");
		test.SendEdits("First frame of the drag");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::SetProperties }, "A preview isn't one SetProperties");

		// A frame without movement sends nothing
		Require(!drag.PreviewChanges("Drag Entity", test.Transactions), "An unchanged value was previewed");
		test.SendEdits("Frame without movement");
		Require(test.TakeSent().empty(), "A frame without movement sent something");

		transform.Position.x = 2.0f;
		drag.PreviewChanges("Drag Entity", test.Transactions);
		test.SendEdits("Second frame of the drag");

		// Previews aren't undo steps; the end of the drag is one
		Require(!test.Transactions.Undo(test.Context), "A preview became an undo step");
		Require(drag.CommitChanges("Translate Entity", test.Transactions), "The end of the drag recorded nothing");
		test.SendEdits("End of the drag");

		Require(test.Transactions.Undo(test.Context) && transform.Position.x == 0.0f, "Undo didn't move the cube back to before the drag");
		test.SendEdits("Undo the drag");

		// Away and back: nothing to undo, and the game shows where the cube is, not where it was dragged
		TransactionDiffUtil roundTrip;
		roundTrip.TakeSnapshot(MakeSceneEntityRef(test.Scene, cube), &transform, transformType);
		transform.Position.y = 5.0f;
		roundTrip.PreviewChanges("Drag Entity", test.Transactions);
		test.SendEdits("Drag away");
		transform.Position.y = 0.0f;
		Require(!roundTrip.CommitChanges("Translate Entity", test.Transactions), "Dragging away and back recorded a step");
		test.SendEdits("Drag back");
		Require(test.Transactions.Redo(test.Context), "Dragging away and back lost the redo of the earlier drag");
		test.SendEdits("Redo the earlier drag");

		// The details panel: a value being dragged there is previewed once per frame too
		const Entity camera = test.Scene.CreateEntity("Camera");
		world.Add<CameraComponent>(camera, CameraComponent{});
		test.Play();
		test.SendEdits("Play again");
		test.TakeSent();

		InspectorDrawContext details;
		details.Transactions = &test.Transactions;
		details.GenericPropertyEdit.bEditing = true;
		details.GenericPropertyEdit.Target = MakeSceneEntityRef(test.Scene, camera);
		details.GenericPropertyEdit.PendingDiff.emplace();
		details.GenericPropertyEdit.PendingDiff->TakeSnapshot(details.GenericPropertyEdit.Target, &world.Get<CameraComponent>(camera),
			Nyx::Reflection::GetTypeMetadata<CameraComponent>());

		world.Get<CameraComponent>(camera).FovYDegrees = 75.0f;
		details.PreviewPendingEdits();
		test.SendEdits("Field of view while dragging");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::SetProperties }, "The details panel's drag wasn't previewed");
	}

	const ComponentTypeOps& GetComponentOps(const Nyx::Reflection::TypeMetadata& type)
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(type);
		Require(ops != nullptr, std::string("Component type isn't registered: ") + type.Name);
		return *ops;
	}

	// Add Component and Remove Component in the details panel: undoable, and the game gets the
	// whole entity each time, since SetProperties can't add or remove components
	void TestAddingAndRemovingComponents()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity lamp = test.Scene.CreateEntity("Lamp");
		world.Add<TransformComponent>(lamp, TransformComponent{});
		const ComponentTypeOps& meshRendererOps = GetComponentOps(Nyx::Reflection::GetTypeMetadata<MeshRendererComponent>());

		test.Play();
		test.SendEdits("Play");
		test.TakeSent();

		// Add: a default MeshRenderer, sent as the whole entity
		Require(AddComponent(test.Scene, test.Transactions, lamp, meshRendererOps), "Adding a MeshRenderer failed");
		Require(world.Has<MeshRendererComponent>(lamp), "The MeshRenderer wasn't added");
		Require(!AddComponent(test.Scene, test.Transactions, lamp, meshRendererOps), "A second MeshRenderer was added");
		test.SendEdits("Add MeshRenderer");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::CreateEntity }, "Adding a component isn't sent as CreateEntity");

		// Give it a mesh, then remove it: undo has to bring the mesh back
		test.Edit(lamp, world.Get<MeshRendererComponent>(lamp).Mesh, "Edit Property",
			[](AssetReference& mesh)
			{
				mesh.Type = "Mesh";
				mesh.Path = "Meshes/Cube.nyxmesh";
			},
			MeshLocation());
		test.SendEdits("Mesh path");
		test.TakeSent();

		Require(RemoveComponent(test.Scene, test.Transactions, lamp, meshRendererOps), "Removing the MeshRenderer failed");
		Require(!world.Has<MeshRendererComponent>(lamp), "The MeshRenderer wasn't removed");
		Require(!RemoveComponent(test.Scene, test.Transactions, lamp, meshRendererOps), "A missing MeshRenderer was removed");
		test.SendEdits("Remove MeshRenderer");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::CreateEntity }, "Removing a component isn't sent as CreateEntity");

		Require(test.Transactions.Undo(test.Context), "Undo of the remove failed");
		Require(world.Has<MeshRendererComponent>(lamp) && world.Get<MeshRendererComponent>(lamp).Mesh.Path == "Meshes/Cube.nyxmesh",
			"Undoing the remove didn't bring the MeshRenderer back with its mesh");
		Require(world.Get<MeshRendererComponent>(lamp).MeshAsset == reinterpret_cast<Nyx::Mesh*>(test.Assets.Fake("Meshes/Cube.nyxmesh")),
			"Undoing the remove didn't load the mesh");
		test.SendEdits("Undo the remove");

		// Undo the mesh path and the add, then redo all three
		Require(test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context), "Undo of the path and the add failed");
		Require(!world.Has<MeshRendererComponent>(lamp), "Undoing the add didn't remove the MeshRenderer");
		test.SendEdits("Undo the path and the add");

		Require(test.Transactions.Redo(test.Context) && world.Has<MeshRendererComponent>(lamp), "Redo of the add failed");
		Require(test.Transactions.Redo(test.Context) && world.Get<MeshRendererComponent>(lamp).Mesh.Path == "Meshes/Cube.nyxmesh",
			"Redo of the mesh path failed");
		Require(test.Transactions.Redo(test.Context) && !world.Has<MeshRendererComponent>(lamp), "Redo of the remove failed");
		test.SendEdits("Redo all three");

		// The guid and the name stay: undo, files and the game find entities by guid
		for (const Nyx::Reflection::TypeMetadata* type :
			{ &Nyx::Reflection::GetTypeMetadata<GuidComponent>(), &Nyx::Reflection::GetTypeMetadata<NameComponent>() })
		{
			const ComponentTypeOps& ops = GetComponentOps(*type);
			Require(!CanAddOrRemoveComponent(ops), std::string(type->Name) + " should be neither addable nor removable");
			Require(!RemoveComponent(test.Scene, test.Transactions, lamp, ops) && ops.Has(world, lamp),
				std::string(type->Name) + " was removed");
		}
	}

	// Duplicate, copy and paste: each copy is a whole entity with a new guid and a name of its own,
	// undoable like Add Entity, and sent to the game as CreateEntity
	void TestDuplicateAndPaste()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<TransformComponent>(cube, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });
		world.Add<MeshRendererComponent>(cube, MeshRendererComponent{
			.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Cube.nyxmesh" },
			.Material = AssetReference{ .Type = "Material", .Path = "Materials/Textured.nyxmat" },
			.bVisible = true });
		const EntityGuid cubeGuid = world.Get<GuidComponent>(cube).Guid;

		test.Play();
		test.SendEdits("Play");
		test.TakeSent();

		// Duplicate: copy and paste in one
		const std::optional<Entity> duplicate =
			PasteEntity(test.Scene, test.Transactions, test.Domain, test.Context, CopyEntity(test.Scene, cube), "Duplicate Entity");
		Require(duplicate.has_value(), "Duplicating failed");
		const EntityGuid duplicateGuid = world.Get<GuidComponent>(*duplicate).Guid;
		Require(duplicateGuid.IsValid() && duplicateGuid != cubeGuid, "A duplicate should get a new guid");
		Require(world.Get<NameComponent>(*duplicate).Name == "Cube (2)", "A duplicate should be named Cube (2)");
		Require(world.Get<TransformComponent>(*duplicate).Position == glm::vec3(1.0f, 2.0f, 3.0f), "A duplicate lost its transform");
		Require(world.Get<MeshRendererComponent>(*duplicate).Mesh.Path == "Meshes/Cube.nyxmesh", "A duplicate lost its mesh");
		Require(world.Get<MeshRendererComponent>(*duplicate).MeshAsset == reinterpret_cast<Nyx::Mesh*>(test.Assets.Fake("Meshes/Cube.nyxmesh")),
			"A duplicate's mesh wasn't loaded");
		test.SendEdits("Duplicate");
		Require(test.TakeSent() == std::vector{ EEditorLinkMessage::CreateEntity }, "A duplicate isn't sent as CreateEntity");

		// Undo and redo, like Add Entity; redo brings back the same copy
		Require(test.Transactions.Undo(test.Context) && !FindByGuid(world, duplicateGuid), "Undo didn't remove the duplicate");
		test.SendEdits("Undo the duplicate");
		Require(test.Transactions.Redo(test.Context) && FindByGuid(world, duplicateGuid), "Redo didn't bring the same duplicate back");
		test.SendEdits("Redo the duplicate");

		// Copy, delete the original, paste twice: each paste is a new entity, never the original's guid
		const std::vector<std::byte> copied = CopyEntity(test.Scene, cube);
		test.DeleteEntity(cube);
		test.SendEdits("Delete the original");

		const std::optional<Entity> first = PasteEntity(test.Scene, test.Transactions, test.Domain, test.Context, copied, "Paste Entity");
		const std::optional<Entity> second = PasteEntity(test.Scene, test.Transactions, test.Domain, test.Context, copied, "Paste Entity");
		Require(first && second, "Pasting failed");
		const EntityGuid firstGuid = world.Get<GuidComponent>(*first).Guid;
		const EntityGuid secondGuid = world.Get<GuidComponent>(*second).Guid;
		Require(firstGuid != cubeGuid && secondGuid != cubeGuid && firstGuid != secondGuid, "Pasted entities should get guids of their own");
		Require(world.Get<NameComponent>(*first).Name == "Cube (3)" && world.Get<NameComponent>(*second).Name == "Cube (4)",
			"Pasted entities should be numbered after the existing copies");
		test.SendEdits("Paste twice");

		// Undoing the delete brings the original back next to its copies; guids stay unique
		Require(test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context) && test.Transactions.Undo(test.Context),
			"Undoing both pastes and the delete failed");
		Require(FindByGuid(world, cubeGuid) && !FindByGuid(world, firstGuid) && !FindByGuid(world, secondGuid),
			"Undo should leave the original and remove the pastes");
		test.SendEdits("Undo the pastes and the delete");

		// Damaged or empty copies paste nothing
		Require(!PasteEntity(test.Scene, test.Transactions, test.Domain, test.Context, {}, "Paste Entity"), "Pasting nothing made an entity");
		std::vector<std::byte> damaged = copied;
		damaged.resize(damaged.size() / 2);
		const size_t entityCount = [&]()
		{
			size_t count = 0;
			world.ForEachEntity([&](Entity) { ++count; });
			return count;
		}();
		Require(!PasteEntity(test.Scene, test.Transactions, test.Domain, test.Context, damaged, "Paste Entity"), "A damaged copy was pasted");
		size_t countAfter = 0;
		world.ForEachEntity([&](Entity) { ++countAfter; });
		Require(countAfter == entityCount, "A damaged paste left an entity behind");
	}

	void TestCopyNames()
	{
		Nyx::SceneDocument scene;
		scene.CreateEntity("Lamp");
		Require(MakeCopyName(scene, "Lamp") == "Lamp (2)", "The first copy should be (2)");
		scene.CreateEntity("Lamp (2)");
		Require(MakeCopyName(scene, "Lamp") == "Lamp (3)" && MakeCopyName(scene, "Lamp (2)") == "Lamp (3)",
			"A copy of a copy should get the next free number");
		Require(MakeCopyName(scene, "Lamp (x)") == "Lamp (x) (2)" && MakeCopyName(scene, "(2)") == "(2) (2)",
			"Only a number in brackets counts as a copy number");
	}

	// The editor reads and writes rotations normalized, through the engine's property access
	void TestReflectedPropertyAccess()
	{
		const auto& type = Nyx::Reflection::GetTypeMetadata<TransformComponent>();
		const auto& rotation = *Nyx::Reflection::FindPropertyByName(type, "Rotation");
		const auto& position = *Nyx::Reflection::FindPropertyByName(type, "Position");

		TransformComponent transform;
		transform.Rotation = glm::quat(2.0f, 0.0f, 0.0f, 0.0f);
		const glm::quat read = std::get<glm::quat>(ReadReflectedPropertyValue(&transform, rotation));
		Require(read == glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "A rotation wasn't read normalized");

		WriteReflectedPropertyValue(&transform, rotation, glm::quat(0.0f, 0.0f, 3.0f, 0.0f));
		Require(transform.Rotation == glm::quat(0.0f, 0.0f, 1.0f, 0.0f), "A rotation wasn't written normalized");

		WriteReflectedPropertyValue(&transform, position, glm::vec3(1.0f, 2.0f, 3.0f));
		Require(std::get<glm::vec3>(ReadReflectedPropertyValue(&transform, position)) == glm::vec3(1.0f, 2.0f, 3.0f), "A position changed on the way");

		bool bThrew = false;
		try
		{
			WriteReflectedPropertyValue(&transform, position, 1.0f);
		}
		catch (const std::bad_variant_access&)
		{
			bThrew = true;
		}
		Require(bThrew, "A value of the wrong kind was written");
	}

	void TestSessions()
	{
		EditorAndGame test;
		Registry& world = test.Scene.GetRegistry();
		const Entity cube = test.Scene.CreateEntity("Cube");
		world.Add<TransformComponent>(cube, TransformComponent{});

		test.Play();
		test.Edit(cube, world.Get<TransformComponent>(cube), "Translate Entity",
			[](TransformComponent& transform) { transform.Position.x = 5.0f; });

		// Stop: what wasn't sent is dropped, and edits from now on aren't kept
		test.GameEdits.EndSession();
		Require(!test.GameEdits.IsSessionActive() && test.GameEdits.TakeMessages().empty(), "Ending the session kept messages");
		test.Edit(cube, world.Get<TransformComponent>(cube), "Translate Entity",
			[](TransformComponent& transform) { transform.Position.x = 6.0f; });
		Require(test.GameEdits.TakeMessages().empty(), "An edit after the session was kept");

		// The next Play starts clean
		test.Play();
		Require(test.GameEdits.IsSessionActive() && test.GameEdits.TakeMessages().empty(), "A new session isn't empty");
		test.SendEdits("Second Play");

		// A game that never takes its edits: past the limit, the session ends instead of growing
		const std::string longName(1024 * 1024, 'x');
		for (size_t i = 0; test.GameEdits.IsSessionActive() && i < 100; ++i)
		{
			test.Edit(cube, world.Get<NameComponent>(cube), "Edit Property",
				[&](NameComponent& name) { name.Name = longName + std::to_string(i); });
		}
		Require(!test.GameEdits.IsSessionActive() && test.GameEdits.TakeMessages().empty(),
			"Edits for a game that doesn't take them piled up without a limit");
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		RegisterComponentTypes();

		TestEditsUndoAndRedo();
		TestAddingAndDeletingEntities();
		TestUndoAfterSlotReuse();
		TestUndoKeepsWholeComponents();
		TestEditsLoadAssets();
		TestValuesWhileDragging();
		TestAddingAndRemovingComponents();
		TestDuplicateAndPaste();
		TestCopyNames();
		TestReflectedPropertyAccess();
		TestSessions();

		std::cout << "All game link subscriber tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game link subscriber test failed: " << error.what() << '\n';
		return 1;
	}
}

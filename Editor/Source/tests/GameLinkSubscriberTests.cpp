// The editor's edits, undo and redo become live edit messages, and a second world (the game's)
// that applies them ends up like the editor's after every step.
#include "AssetReference.h"
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "EditorLinkMessages.h"
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

	// The editor's transaction system as EditorLayer sets it up, and the game's world
	struct EditorAndGame
	{
		Nyx::SceneDocument Scene;
		SceneEntityTransactionDomain Domain;
		TransactionSystem Transactions;
		EditorTransactionContext Context;
		GameLinkSubscriber GameEdits{ Scene };

		Registry Game;
		std::vector<EEditorLinkMessage> Sent;

		EditorAndGame()
		{
			Transactions.RegisterDomain(EObjectDomain::SceneEntity, &Domain);
			Transactions.Subscribe(&GameEdits);
			Context.ActiveScene = &Scene;
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

		// Like the gizmo and the details panel: snapshot, change, commit
		template <typename TObject, typename TChange>
		void Edit(Entity entity, TObject& object, const char* label, TChange change)
		{
			TransactionDiffUtil diff;
			diff.TakeSnapshot(MakeSceneEntityRef(entity), &object, Nyx::Reflection::GetTypeMetadata<TObject>());
			change(object);
			Require(diff.CommitChanges(label, Transactions), std::string(label) + " recorded nothing");
		}

		// Like the outliner's Add Entity button
		Entity AddEntity()
		{
			const Entity entity = Scene.CreateEntity("New Entity");
			const ObjectRef target = MakeSceneEntityRef(entity);

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
			const ObjectRef target = MakeSceneEntityRef(entity);
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
			[](AssetReference& mesh) { mesh.Path = "Sphere"; });
		test.SendEdits("Mesh path");
		Require(test.TakeSent().back() == EEditorLinkMessage::SetProperties, "A struct field edit wasn't sent");

		// The editor's undo can't change struct fields (yet), but the game still shows what the editor has
		test.Transactions.Undo(test.Context);
		test.SendEdits("Undo the mesh path");

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

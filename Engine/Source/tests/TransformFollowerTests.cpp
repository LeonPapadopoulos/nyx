// Children that follow their parents in the game world (ParentComponent::bFollowInGame,
// TransformFollowers): one comes along with a falling body, drawn between its steps, while a child
// that doesn't follow stays; a still parent leaves it still; chains follow, and a flag or local
// values edited live are followed at once; a kinematic follower pushes what it meets; a dynamic one
// never follows; one whose parent is destroyed stays, and follows the parent brought back; a
// spawned prefab's part follows its root; a component pool's version changes with what it holds.
// The players' commands that change parents and take entities away (EntityCommands.h): attached, a
// thing comes along where it was put, and detached it stays; a circle is refused; destroyed, an
// entity goes with what is below it and its bodies; a command names an instance spawned the same step.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "EditorLinkMessages.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "LiveEdits.h"
#include "Log.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "Prefab.h"
#include "PreviousTransformComponent.h"
#include "ReflectionUtils.h"
#include "SimSystem.h"
#include "EntityCommands.h"
#include "SpawnPrefabCommand.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ 1.0 / 60.0, Next, static_cast<double>(Next) / 60.0 });
			}
		}
	};

	// Calls a function each step, before physics
	class CallingSystem final : public ISimSystem
	{
	public:
		explicit CallingSystem(std::function<void(const SimContext&)> call)
			: Call(std::move(call))
		{
		}

		const char* GetName() const override { return "Calling"; }
		void Step(const SimContext& context) override { Call(context); }

	private:
		std::function<void(const SimContext&)> Call;
	};

	class TestPrefabs final : public IAssetResolver
	{
	public:
		Mesh* ResolveMesh(const std::string&) override { return nullptr; }
		Material* ResolveMaterial(const std::string&) override { return nullptr; }
		PrefabAsset* ResolvePrefab(const std::string& path) override
		{
			const auto found = Prefabs.find(path);
			return found != Prefabs.end() ? found->second.get() : nullptr;
		}

		std::map<std::string, std::unique_ptr<PrefabAsset>> Prefabs;
	};

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::length(a - b) <= tolerance;
	}

	std::string Text(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	Entity AddEntity(Registry& world, EntityGuid guid, const glm::vec3& position)
	{
		const Entity entity = world.CreateEntity();
		world.Add<GuidComponent>(entity, GuidComponent{ guid });
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = position });
		return entity;
	}

	// A child placed at `local` from its parent, as the editor would have placed it
	Entity AddChild(Registry& world, EntityGuid guid, EntityGuid parent, const glm::vec3& parentPosition, const glm::vec3& local, bool bFollow)
	{
		const Entity child = AddEntity(world, guid, parentPosition + local);
		world.Add<ParentComponent>(child, ParentComponent{ .Parent = parent, .LocalPosition = local, .bFollowInGame = bFollow });
		return child;
	}

	Entity AddFallingBody(Registry& world, EntityGuid guid, const glm::vec3& position)
	{
		const Entity body = AddEntity(world, guid, position);
		world.Add<BoxColliderComponent>(body, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(body, RigidBodyComponent{ .Mass = 1.0f });
		return body;
	}

	const glm::vec3& PositionOf(GameWorld& game, EntityGuid guid)
	{
		const std::optional<Entity> entity = FindEntityByGuid(game.GetRegistry(), guid);
		Require(entity.has_value(), "The entity " + std::to_string(guid.Value) + " should be there");
		return game.GetRegistry().Get<TransformComponent>(*entity).Position;
	}

	// Sends a property of the editor's entity to the game, as a live edit
	void SendProperty(Registry& editor, Entity entity, GameWorld& game, const char* property)
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<ParentComponent>());
		const std::optional<size_t> index = Nyx::Reflection::FindPropertyIndexByName(Nyx::Reflection::GetTypeMetadata<ParentComponent>(), property);
		Require(ops && index, std::string("The link should have its ") + property);
		const std::optional<SetPropertiesMessage> message = MakeSetPropertiesMessage(editor, entity, *ops, { *index });
		Require(message && ApplyLiveEdit(MakeNetMessage(*message), game.GetRegistry(), {}) == ELiveEditResult::Applied, std::string("The edit of ") + property + " should reach the game");
	}

	void TestAFollowerComesAlongAndOthersStay()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		const EntityGuid crate{ 0x10 }, follower{ 0x11 }, stayer{ 0x12 };
		AddFallingBody(world, crate, glm::vec3(0.0f, 10.0f, 0.0f));
		AddChild(world, follower, crate, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), true);
		AddChild(world, stayer, crate, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), false);
		Steps steps{ game };
		steps.Run(30);
		const glm::vec3 before = PositionOf(game, follower);
		steps.Run(1);

		Require(PositionOf(game, crate).y < 9.0f, "The crate should fall " + Text(PositionOf(game, crate)));
		Require(Near(PositionOf(game, follower), PositionOf(game, crate) + glm::vec3(1.0f, 0.0f, 0.0f)),
			"The follower should come along " + Text(PositionOf(game, follower)));
		Require(Near(PositionOf(game, stayer), glm::vec3(-1.0f, 10.0f, 0.0f)), "A child that doesn't follow should stay where it was placed");
		Require(game.GetFollowers().GetFollowerCount() == 1, "One follower should be found");

		const Entity followerEntity = *FindEntityByGuid(world, follower);
		Require(world.Has<PreviousTransformComponent>(followerEntity) && Near(world.Get<PreviousTransformComponent>(followerEntity).Position, before),
			"The follower should be drawn between where it was and where it is, as its parent is");
	}

	void TestStillParentsLeaveFollowersStill()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		AddEntity(world, EntityGuid{ 0x20 }, glm::vec3(5.0f, 0.0f, 0.0f));
		AddChild(world, EntityGuid{ 0x21 }, EntityGuid{ 0x20 }, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 2.0f, 0.0f), true);
		Steps steps{ game };
		steps.Run(3);
		const Entity child = *FindEntityByGuid(world, EntityGuid{ 0x21 });
		Require(Near(PositionOf(game, EntityGuid{ 0x21 }), glm::vec3(5.0f, 2.0f, 0.0f)) && !world.Has<PreviousTransformComponent>(child),
			"A follower of a still parent should stay, drawn where it is");

		GameWorld empty;
		AddEntity(empty.GetRegistry(), EntityGuid{ 0x22 }, glm::vec3(0.0f));
		Steps emptySteps{ empty };
		emptySteps.Run(2);
		Require(empty.GetFollowers().GetFollowerCount() == 0, "A world without followers should have none");
	}

	void TestChainsAndLiveEdits()
	{
		// The editor's copy and the game's, as the editor link keeps them
		Registry editor;
		const EntityGuid cart{ 0x30 }, post{ 0x31 }, lamp{ 0x32 };
		const glm::vec3 start(0.0f, 20.0f, 0.0f);
		AddFallingBody(editor, cart, start);
		AddChild(editor, post, cart, start, glm::vec3(0.0f, 1.0f, 0.0f), true);
		const Entity lampEntity = AddChild(editor, lamp, post, start + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), true);

		GameWorld game;
		for (const EntityGuid guid : { cart, post, lamp })
		{
			const std::optional<CreateEntityMessage> create = MakeCreateEntityMessage(editor, *FindEntityByGuid(editor, guid));
			Require(create && ApplyLiveEdit(MakeNetMessage(*create), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The entities should reach the game");
		}
		Steps steps{ game };
		steps.Run(20);
		Require(Near(PositionOf(game, lamp), PositionOf(game, cart) + glm::vec3(0.0f, 2.0f, 0.0f)), "A chain of followers should come along " + Text(PositionOf(game, lamp)));

		// Following switched off live: the lamp stays; on again, it comes along
		editor.Get<ParentComponent>(lampEntity).bFollowInGame = false;
		SendProperty(editor, lampEntity, game, "bFollowInGame");
		const glm::vec3 left = PositionOf(game, lamp);
		steps.Run(10);
		Require(Near(PositionOf(game, lamp), left), "A lamp switched off from following should stay");
		editor.Get<ParentComponent>(lampEntity).bFollowInGame = true;
		SendProperty(editor, lampEntity, game, "bFollowInGame");
		steps.Run(1);
		Require(Near(PositionOf(game, lamp), PositionOf(game, cart) + glm::vec3(0.0f, 2.0f, 0.0f)), "Switched on again, the lamp should come along");

		// Its local values edited live: placed by them at once
		editor.Get<ParentComponent>(lampEntity).LocalPosition = glm::vec3(0.0f, 3.0f, 0.0f);
		SendProperty(editor, lampEntity, game, "LocalPosition");
		steps.Run(1);
		Require(Near(PositionOf(game, lamp), PositionOf(game, cart) + glm::vec3(0.0f, 4.0f, 0.0f)), "The lamp should be placed by its edited local values");
	}

	void TestAKinematicFollowerPushes()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		// A floor, a crate on it, and a plough moved by its parent into the crate
		const Entity floor = AddEntity(world, EntityGuid{ 0x40 }, glm::vec3(0.0f, -0.5f, 0.0f));
		world.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(50.0f, 0.5f, 50.0f) });
		AddFallingBody(world, EntityGuid{ 0x41 }, glm::vec3(3.0f, 0.5f, 0.0f));
		const Entity tractor = AddEntity(world, EntityGuid{ 0x42 }, glm::vec3(0.0f, 0.0f, 0.0f));
		const Entity plough = AddChild(world, EntityGuid{ 0x43 }, EntityGuid{ 0x42 }, glm::vec3(0.0f), glm::vec3(0.0f, 0.5f, 0.0f), true);
		world.Add<BoxColliderComponent>(plough, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(plough, RigidBodyComponent{ .bKinematic = true });
		game.AddSystem(std::make_unique<CallingSystem>(
						   [tractor](const SimContext& context)
						   {
							   context.World.Get<TransformComponent>(tractor).Position.x += 2.0f / 60.0f;
						   }),
			1, ESimStage::BeforePhysics);
		Steps steps{ game };
		steps.Run(180);
		Require(PositionOf(game, EntityGuid{ 0x43 }).x > 5.5f, "The plough should come along with its parent " + Text(PositionOf(game, EntityGuid{ 0x43 })));
		Require(PositionOf(game, EntityGuid{ 0x41 }).x > PositionOf(game, EntityGuid{ 0x43 }).x + 0.9f,
			"The kinematic plough should push the crate ahead of it " + Text(PositionOf(game, EntityGuid{ 0x41 })));
	}

	void TestADynamicFollowerDoesntFollow()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		const Entity holder = AddEntity(world, EntityGuid{ 0x50 }, glm::vec3(0.0f, 10.0f, 0.0f));
		const Entity ball = AddChild(world, EntityGuid{ 0x51 }, EntityGuid{ 0x50 }, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), true);
		world.Add<BoxColliderComponent>(ball, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(ball, RigidBodyComponent{ .Mass = 1.0f });
		game.AddSystem(std::make_unique<CallingSystem>(
						   [holder](const SimContext& context)
						   {
							   context.World.Get<TransformComponent>(holder).Position.x += 1.0f / 60.0f;
						   }),
			1, ESimStage::BeforePhysics);
		Steps steps{ game };
		steps.Run(60);
		Require(game.GetFollowers().GetFollowerCount() == 0, "A dynamic body shouldn't be a follower");
		Require(std::abs(PositionOf(game, EntityGuid{ 0x51 }).x) < 1e-3f && PositionOf(game, EntityGuid{ 0x51 }).y < 8.0f,
			"Physics should move the dynamic body, not its parent " + Text(PositionOf(game, EntityGuid{ 0x51 })));
	}

	void TestAParentDestroyedAndBroughtBack()
	{
		Registry editor;
		const EntityGuid crate{ 0x60 }, follower{ 0x61 };
		AddFallingBody(editor, crate, glm::vec3(0.0f, 30.0f, 0.0f));
		AddChild(editor, follower, crate, glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), true);
		GameWorld game;
		for (const EntityGuid guid : { crate, follower })
		{
			const std::optional<CreateEntityMessage> create = MakeCreateEntityMessage(editor, *FindEntityByGuid(editor, guid));
			Require(create && ApplyLiveEdit(MakeNetMessage(*create), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The entities should reach the game");
		}
		Steps steps{ game };
		steps.Run(20);

		// Destroyed: the follower stays where it is
		Require(game.GetRegistry().DestroyEntity(*FindEntityByGuid(game.GetRegistry(), crate)), "Destroying the crate failed");
		const glm::vec3 left = PositionOf(game, follower);
		steps.Run(10);
		Require(Near(PositionOf(game, follower), left) && game.GetFollowers().GetFollowerCount() == 0, "Without its parent, the follower should stay");

		// Brought back, as undo in the editor does: followed again
		const std::optional<CreateEntityMessage> back = MakeCreateEntityMessage(editor, *FindEntityByGuid(editor, crate));
		Require(back && ApplyLiveEdit(MakeNetMessage(*back), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The crate should come back");
		steps.Run(10);
		Require(game.GetFollowers().GetFollowerCount() == 1 && Near(PositionOf(game, follower), PositionOf(game, crate) + glm::vec3(0.0f, 1.0f, 0.0f)),
			"With its parent back, the follower should follow it again " + Text(PositionOf(game, follower)));
	}

	size_t CountEntities(const Registry& world)
	{
		size_t count = 0;
		world.Each<GuidComponent>([&](Entity, const GuidComponent&) { ++count; });
		return count;
	}

	void TestAttachAndDetach()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		const EntityGuid crate{ 0x90 }, lantern{ 0x91 };
		AddFallingBody(world, crate, glm::vec3(0.0f, 30.0f, 0.0f));
		AddEntity(world, lantern, glm::vec3(2.0f, 31.0f, 0.0f));
		Steps steps{ game };

		// Attached: comes along, where it was put relative to the crate
		const glm::vec3 offset = PositionOf(game, lantern) - PositionOf(game, crate);
		game.GetCommands().Add(AttachEntityCommand{ .Entity = lantern, .Parent = crate });
		steps.Run(20);
		Require(PositionOf(game, crate).y < 29.8f && Near(PositionOf(game, lantern), PositionOf(game, crate) + offset),
			"The attached lantern should come along where it was " + Text(PositionOf(game, lantern)));

		// A circle is refused: the crate can't go below its own child
		game.GetCommands().Add(AttachEntityCommand{ .Entity = crate, .Parent = lantern });
		steps.Run(1);
		Require(!world.Has<ParentComponent>(*FindEntityByGuid(world, crate)), "Attaching the crate below its child should be dropped");

		// Detached: stays where it was dropped, drawn there
		game.GetCommands().Add(DetachEntityCommand{ .Entity = lantern });
		steps.Run(1);
		const glm::vec3 dropped = PositionOf(game, lantern);
		steps.Run(10);
		const Entity lanternEntity = *FindEntityByGuid(world, lantern);
		Require(!world.Has<ParentComponent>(lanternEntity) && !world.Has<PreviousTransformComponent>(lanternEntity) && Near(PositionOf(game, lantern), dropped),
			"The detached lantern should stay where it was dropped");
		Require(game.GetFollowers().GetFollowerCount() == 0, "Nothing should follow any more");
	}

	void TestDestroyTakesWhatIsBelow()
	{
		GameWorld game;
		Registry& world = game.GetRegistry();
		const EntityGuid cart{ 0xA0 }, post{ 0xA1 }, lamp{ 0xA2 }, other{ 0xA3 };
		AddFallingBody(world, cart, glm::vec3(0.0f, 30.0f, 0.0f));
		AddChild(world, post, cart, glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), true);
		AddChild(world, lamp, post, glm::vec3(0.0f, 31.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), false);
		AddFallingBody(world, other, glm::vec3(10.0f, 30.0f, 0.0f));
		Steps steps{ game };
		steps.Run(2);
		Require(game.GetBodies().GetBodyCount() == 2, "Both carts should have bodies");

		game.GetCommands().Add(DestroyEntityCommand{ .Entity = cart });
		game.GetCommands().Add(DestroyEntityCommand{ .Entity = EntityGuid{ 0xAF } });
		steps.Run(2);
		Require(CountEntities(world) == 1 && FindEntityByGuid(world, other), "The cart should go with what is below it, the other cart stay");
		Require(game.GetBodies().GetBodyCount() == 1 && game.GetFollowers().GetFollowerCount() == 0, "The cart's body should go with it");
	}

	void TestCommandsNameWhatTheStepSpawned(const std::filesystem::path& folder)
	{
		Registry source;
		const Entity root = AddFallingBody(source, EntityGuid{ 0xB0 }, glm::vec3(0.0f));
		AddChild(source, EntityGuid{ 0xB1 }, EntityGuid{ 0xB0 }, glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), false);
		TestPrefabs prefabs;
		const std::filesystem::path file = folder / "Barrow.nyxprefab";
		Require(PrefabAsset::SaveToFile(source, root, file), "Saving the prefab failed");
		auto prefab = std::make_unique<PrefabAsset>();
		std::string error;
		Require(prefab->LoadFromFile(file, error), "Reading the prefab failed: " + error);
		prefabs.Prefabs["Prefabs/Barrow.nyxprefab"] = std::move(prefab);

		GameWorld game;
		game.SetAssetResolver(&prefabs);
		const EntityGuid kept{ 0xBE }, gone{ 0xBF }, sack{ 0xBD };
		AddEntity(game.GetRegistry(), sack, glm::vec3(0.0f, 21.0f, 0.0f));
		const AssetReference barrow{ "Prefab", "Prefabs/Barrow.nyxprefab" };
		game.GetCommands().Add(SpawnPrefabCommand{ .Prefab = barrow, .Guid = kept, .Position = glm::vec3(0.0f, 20.0f, 0.0f) });
		game.GetCommands().Add(SpawnPrefabCommand{ .Prefab = barrow, .Guid = gone, .Position = glm::vec3(5.0f, 20.0f, 0.0f) });
		game.GetCommands().Add(DestroyEntityCommand{ .Entity = gone });
		game.GetCommands().Add(AttachEntityCommand{ .Entity = sack, .Parent = kept });
		Steps steps{ game };
		steps.Run(1);
		Require(!FindEntityByGuid(game.GetRegistry(), gone) && !FindEntityByGuid(game.GetRegistry(), GetPrefabPartGuid(gone, EntityGuid{ 0xB1 })),
			"An instance destroyed the step it was spawned should go with its part");
		steps.Run(20);
		Require(PositionOf(game, kept).y < 19.5f && Near(PositionOf(game, sack), PositionOf(game, kept) + glm::vec3(0.0f, 1.0f, 0.0f)),
			"A sack attached the step its barrow was spawned should come along " + Text(PositionOf(game, sack)));
	}

	void TestASpawnedPartFollows(const std::filesystem::path& folder)
	{
		Registry source;
		const Entity root = AddFallingBody(source, EntityGuid{ 0x70 }, glm::vec3(0.0f));
		AddChild(source, EntityGuid{ 0x71 }, EntityGuid{ 0x70 }, glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), true);
		TestPrefabs prefabs;
		const std::filesystem::path file = folder / "Cart.nyxprefab";
		Require(PrefabAsset::SaveToFile(source, root, file), "Saving the prefab failed");
		auto prefab = std::make_unique<PrefabAsset>();
		std::string error;
		Require(prefab->LoadFromFile(file, error), "Reading the prefab failed: " + error);
		prefabs.Prefabs["Prefabs/Cart.nyxprefab"] = std::move(prefab);

		GameWorld game;
		game.SetAssetResolver(&prefabs);
		const EntityGuid instance{ 0x7F };
		game.GetCommands().Add(SpawnPrefabCommand{ .Prefab = AssetReference{ "Prefab", "Prefabs/Cart.nyxprefab" }, .Guid = instance, .Position = glm::vec3(0.0f, 15.0f, 0.0f) });
		Steps steps{ game };
		steps.Run(30);
		const EntityGuid part = GetPrefabPartGuid(instance, EntityGuid{ 0x71 });
		Require(PositionOf(game, instance).y < 14.0f && Near(PositionOf(game, part), PositionOf(game, instance) + glm::vec3(0.0f, 2.0f, 0.0f)),
			"The spawned cart's part should follow it as it falls " + Text(PositionOf(game, part)));
	}

	void TestPoolVersions()
	{
		Registry world;
		Require(world.GetVersion<ParentComponent>() == 0, "Without links, the version should be 0");
		const Entity entity = AddEntity(world, EntityGuid{ 0x80 }, glm::vec3(0.0f));
		world.Add<ParentComponent>(entity, ParentComponent{});
		const uint64_t added = world.GetVersion<ParentComponent>();
		world.Get<ParentComponent>(entity).LocalPosition = glm::vec3(1.0f);
		Require(added != 0 && world.GetVersion<ParentComponent>() == added, "Values changed in place shouldn't change the version");
		world.MarkChanged<ParentComponent>();
		const uint64_t marked = world.GetVersion<ParentComponent>();
		Require(marked != added, "MarkChanged() should change it");
		world.Get<TransformComponent>(entity).Position = glm::vec3(2.0f);
		Require(world.GetVersion<ParentComponent>() == marked, "Another component's changes shouldn't change it");
		world.DestroyEntity(entity);
		Require(world.GetVersion<ParentComponent>() != marked, "Destroying an entity with a link should change it");

		Registry other;
		const Entity again = AddEntity(other, EntityGuid{ 0x81 }, glm::vec3(0.0f));
		other.Add<ParentComponent>(again, ParentComponent{});
		Require(other.GetVersion<ParentComponent>() != world.GetVersion<ParentComponent>(), "Two registries should never share a version");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxTransformFollowerTests");
		TestAFollowerComesAlongAndOthersStay();
		TestStillParentsLeaveFollowersStill();
		TestChainsAndLiveEdits();
		TestAKinematicFollowerPushes();
		TestADynamicFollowerDoesntFollow();
		TestAParentDestroyedAndBroughtBack();
		TestASpawnedPartFollows(directory.Path);
		TestPoolVersions();
		TestAttachAndDetach();
		TestDestroyTakesWhatIsBelow();
		TestCommandsNameWhatTheStepSpawned(directory.Path);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << std::endl;
		return 1;
	}

	std::cout << "All transform follower tests passed." << std::endl;
	return 0;
}

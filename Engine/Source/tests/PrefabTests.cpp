// Prefabs: an entity and what is below it saved as a prefab and read back; an instance made from it,
// its parts with guids made from its own and placed by it; a scene with instances saved as what
// differs from the prefab and loaded back the same, also after the prefab changed under the
// instance's overrides; components added to and taken from an instance; an instance whose prefab
// is gone; instances spawned by players' commands in a game's world.
#include "BinaryArchive.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "NameComponent.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "PointLightComponent.h"
#include "Prefab.h"
#include "SceneSerializer.h"
#include "SpawnPrefabCommand.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::Entity;
	using Nyx::EntityGuid;
	using Nyx::ParentComponent;
	using Nyx::PrefabAsset;
	using Nyx::PrefabInstanceComponent;
	using Nyx::PrefabPartComponent;
	using Nyx::Registry;
	using Nyx::TransformComponent;

	using Nyx::Tests::Require;

	constexpr const char* LampPath = "Prefabs/Lamp.nyxprefab";

	// Hands out the prefabs it was given, by path
	class TestPrefabs final : public Nyx::IAssetResolver
	{
	public:
		Nyx::Mesh* ResolveMesh(const std::string&) override
		{
			return nullptr;
		}
		Nyx::Material* ResolveMaterial(const std::string&) override
		{
			return nullptr;
		}
		const PrefabAsset* ResolvePrefab(const std::string& path) override
		{
			const auto found = Prefabs.find(path);
			return found != Prefabs.end() ? found->second.get() : nullptr;
		}

		std::map<std::string, std::unique_ptr<PrefabAsset>> Prefabs;
	};

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < 1e-4f;
	}

	Entity CreateEntity(Registry& world, const char* name, EntityGuid guid, const glm::vec3& position)
	{
		const Entity entity = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ guid });
		world.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ name });
		TransformComponent transform;
		transform.Position = position;
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	// A lamp: a post with a collider, an arm on it, a light at the arm's end
	struct Lamp
	{
		Registry World;
		Entity Post{};
		Entity Arm{};
		Entity Light{};
	};

	std::unique_ptr<Lamp> MakeLamp()
	{
		auto lamp = std::make_unique<Lamp>();
		Registry& world = lamp->World;
		lamp->Post = CreateEntity(world, "Post", EntityGuid{ 0x10 }, glm::vec3(50.0f, 0.0f, 0.0f));
		world.Add<Nyx::BoxColliderComponent>(lamp->Post, Nyx::BoxColliderComponent{ .HalfExtents = glm::vec3(0.1f, 1.5f, 0.1f) });
		world.Add<Nyx::PointLightComponent>(lamp->Post, Nyx::PointLightComponent{});
		lamp->Arm = CreateEntity(world, "Arm", EntityGuid{ 0x20 }, glm::vec3(0.0f));
		world.Add<ParentComponent>(lamp->Arm, ParentComponent{ .Parent = EntityGuid{ 0x10 }, .LocalPosition = glm::vec3(0.0f, 3.0f, 0.0f) });
		lamp->Light = CreateEntity(world, "Light", EntityGuid{ 0x30 }, glm::vec3(0.0f));
		world.Add<ParentComponent>(lamp->Light, ParentComponent{ .Parent = EntityGuid{ 0x20 }, .LocalPosition = glm::vec3(1.0f, 0.0f, 0.0f) });
		Nyx::PointLightComponent light;
		light.Intensity = 5.0f;
		world.Add<Nyx::PointLightComponent>(lamp->Light, light);
		return lamp;
	}

	std::unique_ptr<PrefabAsset> SaveAndLoad(const Registry& world, Entity root, const std::filesystem::path& path)
	{
		Require(PrefabAsset::SaveToFile(world, root, path), "Saving a prefab failed");
		auto prefab = std::make_unique<PrefabAsset>();
		std::string error;
		Require(prefab->LoadFromFile(path, error), "Loading a prefab failed: " + error);
		return prefab;
	}

	// An instance of the prefab at the guid, where it is placed
	Entity Place(Registry& world, const PrefabAsset& prefab, EntityGuid guid, const glm::vec3& position, std::vector<Entity>* outParts = nullptr)
	{
		const Entity root = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(root, Nyx::GuidComponent{ guid });
		TransformComponent transform;
		transform.Position = position;
		world.Add<TransformComponent>(root, transform);
		world.Add<PrefabInstanceComponent>(root, PrefabInstanceComponent{ .Prefab = Nyx::AssetReference{ "Prefab", LampPath } });
		Nyx::ScenePostLoadContext context{};
		Nyx::ApplyPrefab(world, root, prefab, context, outParts);
		return root;
	}

	std::optional<Entity> Find(const Registry& world, EntityGuid guid)
	{
		return Nyx::FindEntityByGuid(world, guid);
	}

	size_t CountEntities(const Registry& world)
	{
		size_t count = 0;
		world.ForEachEntity([&](Entity) { ++count; });
		return count;
	}

	// Every entity as scene files store it whole, by guid: two worlds with the same are the same
	std::map<uint64_t, std::vector<std::byte>> Describe(const Registry& world)
	{
		std::map<uint64_t, std::vector<std::byte>> entities;
		world.ForEachEntity([&](Entity entity)
			{
				Nyx::BinaryWriter writer;
				Require(Nyx::SceneSerializer::WriteEntity(world, entity, writer), "An entity can't be written");
				entities[world.Get<Nyx::GuidComponent>(entity).Guid.Value] = writer.GetBytes();
			});
		return entities;
	}

	Registry Load(const std::filesystem::path& path, TestPrefabs& prefabs)
	{
		Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(path, loaded, Nyx::ScenePostLoadContext{ .AssetResolver = &prefabs }), "Loading a scene with instances failed");
		return loaded;
	}

	void TestSavedAndRead()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		const std::unique_ptr<Lamp> lamp = MakeLamp();
		const std::unique_ptr<PrefabAsset> prefab = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");

		Require(prefab->GetEntities().size() == 3, "The prefab should have the post and both below it");
		const Registry& world = prefab->GetTemplate();
		Require(world.Get<Nyx::GuidComponent>(prefab->GetRoot()).Guid == EntityGuid{ 0x10 }, "The root should come first, with its id");
		Require(world.Has<Nyx::BoxColliderComponent>(prefab->GetRoot()) && world.Has<Nyx::PointLightComponent>(prefab->GetRoot()),
			"The root should keep its components");
		const std::optional<Entity> light = prefab->FindLocal(EntityGuid{ 0x30 });
		Require(light && world.Get<ParentComponent>(*light).Parent == EntityGuid{ 0x20 } && world.Get<Nyx::PointLightComponent>(*light).Intensity == 5.0f,
			"A part should keep its parent's id and its values");

		// The root without a parent: an instance has its own
		Registry parented;
		const Entity holder = CreateEntity(parented, "Holder", EntityGuid{ 0x1 }, glm::vec3(0.0f));
		const Entity child = CreateEntity(parented, "Child", EntityGuid{ 0x2 }, glm::vec3(0.0f));
		parented.Add<ParentComponent>(child, ParentComponent{ .Parent = EntityGuid{ 0x1 } });
		(void)holder;
		const std::unique_ptr<PrefabAsset> unparented = SaveAndLoad(parented, child, directory.Path / "Child.nyxprefab");
		Require(!unparented->GetTemplate().Has<ParentComponent>(unparented->GetRoot()), "A prefab's root should have no parent");

		// An instance can't go into a prefab yet
		Registry nested;
		const Entity instance = CreateEntity(nested, "Instance", EntityGuid{ 0x3 }, glm::vec3(0.0f));
		nested.Add<PrefabInstanceComponent>(instance, PrefabInstanceComponent{});
		Require(!PrefabAsset::SaveToFile(nested, instance, directory.Path / "Nested.nyxprefab"), "A prefab holding an instance should be refused");

		std::string error;
		PrefabAsset notOne;
		Require(!notOne.LoadFromFile(directory.Path / "Missing.nyxprefab", error), "A missing file should be no prefab");
	}

	void TestAnInstanceIsMadeFromItsPrefab()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		const std::unique_ptr<Lamp> lamp = MakeLamp();
		const std::unique_ptr<PrefabAsset> prefab = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");

		Registry world;
		std::vector<Entity> parts;
		const Entity root = Place(world, *prefab, EntityGuid{ 0xA1 }, glm::vec3(0.0f, 0.0f, 10.0f), &parts);
		Require(world.Get<Nyx::GuidComponent>(root).Guid == EntityGuid{ 0xA1 } && Near(world.Get<TransformComponent>(root).Position, glm::vec3(0.0f, 0.0f, 10.0f)),
			"The root should keep its guid and where it is");
		Require(world.Has<Nyx::BoxColliderComponent>(root) && world.Get<Nyx::NameComponent>(root).Name == "Post", "The root should get the prefab root's components");
		Require(parts.size() == 2 && CountEntities(world) == 3, "The instance should make two parts");

		const EntityGuid armGuid = Nyx::GetPrefabPartGuid(EntityGuid{ 0xA1 }, EntityGuid{ 0x20 });
		const EntityGuid lightGuid = Nyx::GetPrefabPartGuid(EntityGuid{ 0xA1 }, EntityGuid{ 0x30 });
		const std::optional<Entity> light = Find(world, lightGuid);
		Require(Find(world, armGuid) && light, "The parts' guids should be made from the instance's");
		Require(world.Get<ParentComponent>(*light).Parent == armGuid, "A part's parent should be the instance's part");
		Require(world.Get<PrefabPartComponent>(*light).Instance == EntityGuid{ 0xA1 } && world.Get<PrefabPartComponent>(*light).Local == EntityGuid{ 0x30 },
			"A part should say which instance and prefab entity it is");
		Require(Near(world.Get<TransformComponent>(*light).Position, glm::vec3(1.0f, 3.0f, 10.0f)), "The parts should be placed by the instance's root");

		// Another instance: other guids, the same every time
		Place(world, *prefab, EntityGuid{ 0xB2 }, glm::vec3(0.0f));
		Require(Nyx::GetPrefabPartGuid(EntityGuid{ 0xB2 }, EntityGuid{ 0x30 }) != lightGuid, "Two instances' parts should have different guids");
		Require(Nyx::GetPrefabPartGuid(EntityGuid{ 0xA1 }, EntityGuid{ 0x30 }) == lightGuid, "A part's guid should be the same every time");
		Require(CountEntities(world) == 6, "Each instance makes its own parts");
	}

	void TestScenesKeepOnlyWhatDiffers()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		const std::unique_ptr<Lamp> lamp = MakeLamp();
		TestPrefabs prefabs;
		prefabs.Prefabs[LampPath] = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");
		const PrefabAsset& prefab = *prefabs.Prefabs[LampPath];

		// Unchanged instances: only their roots are written, and they load as they were
		Registry world;
		for (uint64_t index = 1; index <= 20; ++index)
		{
			Place(world, prefab, EntityGuid{ 0x1000 + index }, glm::vec3(static_cast<float>(index), 0.0f, 0.0f));
		}
		const std::filesystem::path compact = directory.Path / "Compact.nyxscene";
		const std::filesystem::path whole = directory.Path / "Whole.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, compact, nullptr, &prefabs), "Saving instances failed");
		Require(Nyx::SceneSerializer::SaveToFile(world, whole), "Saving instances whole failed");
		Require(std::filesystem::file_size(compact) * 3 < std::filesystem::file_size(whole),
			"Instances saved against their prefab should take far less than whole: " + std::to_string(std::filesystem::file_size(compact)) +
				" bytes, whole " + std::to_string(std::filesystem::file_size(whole)));
		Require(Describe(Load(compact, prefabs)) == Describe(world), "Unchanged instances should load as they were saved");
		Require(Describe(Load(whole, prefabs)) == Describe(world), "Instances saved whole should load as they were saved too");

		// Overrides: a value of the root, a part moved and its light changed, a component added to the
		// root, one taken from it and one from a part
		Registry edited;
		const Entity root = Place(edited, prefab, EntityGuid{ 0xC3 }, glm::vec3(0.0f, 0.0f, 5.0f));
		const Entity arm = *Find(edited, Nyx::GetPrefabPartGuid(EntityGuid{ 0xC3 }, EntityGuid{ 0x20 }));
		const Entity light = *Find(edited, Nyx::GetPrefabPartGuid(EntityGuid{ 0xC3 }, EntityGuid{ 0x30 }));
		edited.Get<Nyx::BoxColliderComponent>(root).HalfExtents = glm::vec3(0.2f, 2.0f, 0.2f);
		edited.Get<ParentComponent>(arm).LocalPosition = glm::vec3(0.0f, 4.0f, 0.0f);
		edited.Get<Nyx::PointLightComponent>(light).Color = glm::vec3(1.0f, 0.5f, 0.2f);
		edited.Add<Nyx::SphereColliderComponent>(root, Nyx::SphereColliderComponent{});
		edited.Remove<Nyx::PointLightComponent>(root);
		edited.Remove<Nyx::NameComponent>(arm);
		// As saving works them out, and loading keeps them
		edited.Get<PrefabInstanceComponent>(root).RemovedComponents = { Nyx::Reflection::GetTypeMetadata<Nyx::PointLightComponent>().Name };
		edited.Get<PrefabPartComponent>(arm).RemovedComponents = { Nyx::Reflection::GetTypeMetadata<Nyx::NameComponent>().Name };
		Nyx::UpdateTransformHierarchy(edited, Nyx::MakeEntityGuidLookup(edited));
		const std::filesystem::path overridden = directory.Path / "Overridden.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(edited, overridden, nullptr, &prefabs), "Saving an edited instance failed");
		const Registry loaded = Load(overridden, prefabs);
		Require(Describe(loaded) == Describe(edited), "An edited instance should load with its edits");
		const Entity loadedLight = *Find(loaded, Nyx::GetPrefabPartGuid(EntityGuid{ 0xC3 }, EntityGuid{ 0x30 }));
		Require(Near(loaded.Get<TransformComponent>(loadedLight).Position, glm::vec3(1.0f, 4.0f, 5.0f)), "Its parts should be placed as edited");
	}

	void TestOverridesOutliveChangesOfThePrefab()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		std::unique_ptr<Lamp> lamp = MakeLamp();
		TestPrefabs prefabs;
		prefabs.Prefabs[LampPath] = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");

		// The instance makes its light red; the scene is saved
		Registry world;
		const Entity root = Place(world, *prefabs.Prefabs[LampPath], EntityGuid{ 0xD4 }, glm::vec3(0.0f));
		const EntityGuid lightGuid = Nyx::GetPrefabPartGuid(EntityGuid{ 0xD4 }, EntityGuid{ 0x30 });
		world.Get<Nyx::PointLightComponent>(*Find(world, lightGuid)).Color = glm::vec3(1.0f, 0.0f, 0.0f);
		(void)root;
		const std::filesystem::path scene = directory.Path / "Lamps.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, scene, nullptr, &prefabs), "Saving the scene failed");

		// The prefab changes: a brighter, bluer light, a taller arm, a new component on the post
		Registry& prefabWorld = lamp->World;
		prefabWorld.Get<Nyx::PointLightComponent>(lamp->Light).Intensity = 9.0f;
		prefabWorld.Get<Nyx::PointLightComponent>(lamp->Light).Color = glm::vec3(0.0f, 0.0f, 1.0f);
		prefabWorld.Get<ParentComponent>(lamp->Arm).LocalPosition = glm::vec3(0.0f, 6.0f, 0.0f);
		prefabWorld.Add<Nyx::RigidBodyComponent>(lamp->Post, Nyx::RigidBodyComponent{});
		prefabs.Prefabs[LampPath] = SaveAndLoad(prefabWorld, lamp->Post, directory.Path / "Lamp.nyxprefab");

		const Registry loaded = Load(scene, prefabs);
		const Entity light = *Find(loaded, lightGuid);
		Require(loaded.Get<Nyx::PointLightComponent>(light).Color == glm::vec3(1.0f, 0.0f, 0.0f), "The instance's own colour should stay");
		Require(loaded.Get<Nyx::PointLightComponent>(light).Intensity == 9.0f, "A value the instance didn't change should follow the prefab");
		Require(Near(loaded.Get<TransformComponent>(light).Position, glm::vec3(1.0f, 6.0f, 0.0f)), "The parts should follow the prefab's new arm");
		Require(loaded.Has<Nyx::RigidBodyComponent>(*Find(loaded, EntityGuid{ 0xD4 })), "A component the prefab gained should reach the instance");
	}

	void TestAnInstanceWithoutItsPrefab()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		const std::unique_ptr<Lamp> lamp = MakeLamp();
		TestPrefabs prefabs;
		prefabs.Prefabs[LampPath] = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");
		Registry world;
		const Entity root = Place(world, *prefabs.Prefabs[LampPath], EntityGuid{ 0xE5 }, glm::vec3(2.0f, 0.0f, 0.0f));
		world.Get<Nyx::PointLightComponent>(*Find(world, Nyx::GetPrefabPartGuid(EntityGuid{ 0xE5 }, EntityGuid{ 0x30 }))).Range = 20.0f;
		(void)root;
		const std::filesystem::path scene = directory.Path / "Orphans.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, scene, nullptr, &prefabs), "Saving the scene failed");

		// Gone: the root loads with what the scene has of it, nothing breaks
		TestPrefabs none;
		const Registry loaded = Load(scene, none);
		const std::optional<Entity> loadedRoot = Find(loaded, EntityGuid{ 0xE5 });
		Require(loadedRoot && loaded.Has<PrefabInstanceComponent>(*loadedRoot) && Near(loaded.Get<TransformComponent>(*loadedRoot).Position, glm::vec3(2.0f, 0.0f, 0.0f)),
			"An instance whose prefab is gone should still load its root, where it was");
		Require(Find(loaded, Nyx::GetPrefabPartGuid(EntityGuid{ 0xE5 }, EntityGuid{ 0x30 })).has_value(),
			"A part the scene has values of should keep its guid without its prefab");
	}

	size_t CountEntities(const Nyx::GameWorld& world)
	{
		return CountEntities(world.GetRegistry());
	}

	Nyx::SpawnPrefabCommand MakeSpawn(const char* path, EntityGuid guid, const glm::vec3& position)
	{
		return Nyx::SpawnPrefabCommand{ .Prefab = Nyx::AssetReference{ "Prefab", path }, .Guid = guid, .Position = position };
	}

	// A player's commands make instances at the start of the next step; a taken guid, no guid or a
	// prefab that isn't there drops one. What a spawned instance has works as any entity: its body
	// falls.
	void TestCommandsSpawnInstances()
	{
		Nyx::Tests::TestDirectory directory("NyxPrefabTests");
		std::unique_ptr<Lamp> lamp = MakeLamp();
		lamp->World.Add<Nyx::RigidBodyComponent>(lamp->Post, Nyx::RigidBodyComponent{});
		TestPrefabs prefabs;
		prefabs.Prefabs[LampPath] = SaveAndLoad(lamp->World, lamp->Post, directory.Path / "Lamp.nyxprefab");

		// Without anything to resolve prefabs, nothing is spawned
		{
			Nyx::GameWorld world;
			world.GetCommands().Add(MakeSpawn(LampPath, EntityGuid{ 0xF1 }, glm::vec3(0.0f)));
			world.Step(Nyx::StepTime{ 1.0 / 60.0, 0, 0.0 });
			Require(CountEntities(world) == 0, "Without prefabs, a spawn should be dropped");
		}

		Nyx::GameWorld world;
		world.SetAssetResolver(&prefabs);
		world.GetCommands().Add(MakeSpawn(LampPath, EntityGuid{ 0xF1 }, glm::vec3(0.0f, 10.0f, 0.0f)));
		world.GetCommands().Add(MakeSpawn(LampPath, EntityGuid{ 0xF1 }, glm::vec3(5.0f, 10.0f, 0.0f)));
		world.GetCommands().Add(MakeSpawn(LampPath, EntityGuid{}, glm::vec3(0.0f)));
		world.GetCommands().Add(MakeSpawn("Prefabs/Missing.nyxprefab", EntityGuid{ 0xF2 }, glm::vec3(0.0f)));
		Require(CountEntities(world) == 0, "Commands should wait for the next step");
		world.Step(Nyx::StepTime{ 1.0 / 60.0, 0, 0.0 });

		const Registry& registry = world.GetRegistry();
		Require(CountEntities(world) == 3, "One instance, its root and two parts, should be spawned; the others dropped");
		const std::optional<Entity> root = Find(registry, EntityGuid{ 0xF1 });
		Require(root && registry.Has<PrefabInstanceComponent>(*root) && registry.Has<Nyx::BoxColliderComponent>(*root),
			"The instance's root should have the guid asked for and its prefab's components");
		Require(Near(registry.Get<TransformComponent>(*root).Position, glm::vec3(0.0f, 10.0f, 0.0f)), "The root should be where it was asked");
		const std::optional<Entity> light = Find(registry, Nyx::GetPrefabPartGuid(EntityGuid{ 0xF1 }, EntityGuid{ 0x30 }));
		Require(light && Near(registry.Get<TransformComponent>(*light).Position, glm::vec3(1.0f, 13.0f, 0.0f)),
			"A part should have its guid made from the root's, placed by it");

		// A guid spawned in an earlier step is taken too
		world.GetCommands().Add(MakeSpawn(LampPath, EntityGuid{ 0xF1 }, glm::vec3(0.0f)));
		world.Step(Nyx::StepTime{ 1.0 / 60.0, 1, 1.0 / 60.0 });
		Require(CountEntities(world) == 3, "A spawn with a guid the world has should be dropped");

		// Its body is made, and falls
		for (uint64_t step = 2; step < 30; ++step)
		{
			world.Step(Nyx::StepTime{ 1.0 / 60.0, step, static_cast<double>(step) / 60.0 });
		}
		Require(registry.Get<TransformComponent>(*Find(registry, EntityGuid{ 0xF1 })).Position.y < 9.5f, "A spawned rigid body should fall");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestSavedAndRead();
		TestAnInstanceIsMadeFromItsPrefab();
		TestScenesKeepOnlyWhatDiffers();
		TestOverridesOutliveChangesOfThePrefab();
		TestAnInstanceWithoutItsPrefab();
		TestCommandsSpawnInstances();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All prefab tests passed\n";
	return 0;
}

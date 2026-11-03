// Save games: a world saved while it runs and loaded into a new one, as what differs from its scene,
// with entities made at run time, changed components and destroyed entities coming back by guid; a
// save of a loaded world still a difference from the scene; a scene changed under a save; children
// left behind by their parents staying there; the clock and the time of day going on; a save made on
// another thread from a snapshot; sections and types a build doesn't know, later versions, damage and
// a missing scene; instances of prefabs made at run time as what differs from their prefabs, following
// changes to them; those of the scene; bodies and characters going on as fast as they went; a terrain's
// edits on their own, there to undo, and loading when the ground under them has changed. With
// --measure, in a Release build, what taking a snapshot costs.
#include "BinaryArchive.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "EntityCommands.h"
#include "EntityGuidLookup.h"
#include "FixedStepClock.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "PointLightComponent.h"
#include "Prefab.h"
#include "ReflectionUtils.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "SdfTerrainEditCommand.h"
#include "SpawnPrefabCommand.h"
#include "TerrainSave.h"
#include "TestSupport.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "WorldSave.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Nyx::Entity;
	using Nyx::EntityGuid;
	using Nyx::GameWorld;
	using Nyx::ParentComponent;
	using Nyx::PointLightComponent;
	using Nyx::PrefabAsset;
	using Nyx::PrefabInstanceComponent;
	using Nyx::PrefabPartComponent;
	using Nyx::Registry;
	using Nyx::TransformComponent;
	using Nyx::WorldSaveBase;
	using Nyx::WorldSaveClock;

	using Nyx::Tests::Require;
	using Nyx::Tests::TestDirectory;

	constexpr EntityGuid CrateGuid{ 101 };
	constexpr EntityGuid LampGuid{ 102 };
	constexpr EntityGuid SignGuid{ 103 };
	constexpr EntityGuid DoomedGuid{ 104 };
	constexpr EntityGuid PlainGuid{ 105 };
	constexpr EntityGuid MadeGuid{ 201 };
	constexpr double Step = 1.0 / 60.0;

	Entity Add(Registry& world, EntityGuid guid, const char* name, const glm::vec3& position = glm::vec3(0.0f))
	{
		const Entity entity = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ guid });
		world.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ name });
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = position });
		return entity;
	}

	Entity Find(const Registry& world, EntityGuid guid)
	{
		const std::optional<Entity> entity = Nyx::FindEntityByGuid(world, guid);
		Require(entity.has_value(), "The world should have entity " + Nyx::GuidToText(guid));
		return *entity;
	}

	// A falling crate, a lamp, a sign with a light, one to be destroyed, and one nothing happens to
	std::filesystem::path WriteScene(const std::filesystem::path& folder)
	{
		Registry scene;
		const Entity crate = Add(scene, CrateGuid, "Crate", glm::vec3(0.0f, 50.0f, 0.0f));
		scene.Add<Nyx::BoxColliderComponent>(crate);
		scene.Add<Nyx::RigidBodyComponent>(crate);
		const Entity lamp = Add(scene, LampGuid, "Lamp", glm::vec3(5.0f, 0.0f, 0.0f));
		scene.Add<PointLightComponent>(lamp, PointLightComponent{ .Intensity = 3.0f });
		const Entity sign = Add(scene, SignGuid, "Sign");
		scene.Add<PointLightComponent>(sign);
		Add(scene, DoomedGuid, "Doomed");
		Add(scene, PlainGuid, "Plain");

		const std::filesystem::path path = folder / "World.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(scene, path), "The scene should be saved");
		return path;
	}

	// A new game's world from the scene, with what its next save is a difference from
	void StartWorld(GameWorld& world, const std::filesystem::path& scene, WorldSaveBase& outBase)
	{
		Require(Nyx::SceneSerializer::LoadFromFile(scene, world.GetRegistry()), "The scene should load");
		Require(Nyx::MakeWorldSaveBase(scene, world.GetRegistry(), outBase), "The world should give a base");
	}

	WorldSaveClock RunSteps(GameWorld& world, uint64_t from, uint64_t count)
	{
		for (uint64_t index = from; index < from + count; ++index)
		{
			world.Step(Nyx::StepTime{ Step, index, static_cast<double>(index) * Step });
		}
		return WorldSaveClock{ from + count, static_cast<double>(from + count) * Step, Step };
	}

	void MustLoad(const std::filesystem::path& path, Registry& world, WorldSaveBase& outBase, WorldSaveClock& outClock)
	{
		std::string error;
		Require(Nyx::LoadWorldSave(path, world, {}, outBase, outClock, error), "The save should load, but it " + error);
	}

	// Whether two worlds hold the same saved values, entity by entity, whatever their order; of the
	// marks of prefab instances, not the lists of what they took away, which only a save works out
	void RequireSameWorld(const Registry& a, const Registry& b, const std::string& what)
	{
		Nyx::WorldSnapshot first;
		Nyx::WorldSnapshot second;
		Require(Nyx::TakeWorldSnapshot(a, first) && Nyx::TakeWorldSnapshot(b, second), "Both worlds should give a snapshot");
		Require(first.Entities.size() == second.Entities.size(), what + ": the worlds should have as many entities with guids");

		const auto bytesOf = [](const Nyx::WorldSnapshot& snapshot, const Nyx::WorldSnapshot::Component& component)
		{
			return std::span<const std::byte>(snapshot.Bytes.GetBytes()).subspan(component.Offset, component.Size);
		};
		for (const Nyx::WorldSnapshot::SavedEntity& entity : first.Entities)
		{
			const auto other = std::find_if(second.Entities.begin(), second.Entities.end(),
				[&](const Nyx::WorldSnapshot::SavedEntity& candidate)
				{
					return candidate.Guid == entity.Guid;
				});
			Require(other != second.Entities.end(), what + ": entity " + Nyx::GuidToText(entity.Guid) + " should be in both worlds");
			Require(other->ComponentCount == entity.ComponentCount, what + ": entity " + Nyx::GuidToText(entity.Guid) + " should have the same components");
			for (uint32_t index = 0; index < entity.ComponentCount; ++index)
			{
				const Nyx::WorldSnapshot::Component& component = first.Components[entity.FirstComponent + index];
				const Nyx::WorldSnapshot::Component& otherComponent = second.Components[other->FirstComponent + index];
				const auto bytes = bytesOf(first, component);
				const auto otherBytes = bytesOf(second, otherComponent);
				if (component.Type == &Nyx::Reflection::GetTypeMetadata<PrefabInstanceComponent>() || component.Type == &Nyx::Reflection::GetTypeMetadata<PrefabPartComponent>())
				{
					Require(component.Type == otherComponent.Type, what + ": the marks of entity " + Nyx::GuidToText(entity.Guid) + " should be the same");
					continue;
				}
				Require(component.Type == otherComponent.Type && bytes.size() == otherBytes.size() && std::memcmp(bytes.data(), otherBytes.data(), bytes.size()) == 0,
					what + ": " + component.Type->Name + " of entity " + Nyx::GuidToText(entity.Guid) + " should be the same");
			}
		}
	}

	// What a running world does in its first steps: everything a save has to tell apart
	void ChangeTheWorld(GameWorld& world)
	{
		Registry& registry = world.GetRegistry();

		// Changed: one property of a component
		registry.Get<PointLightComponent>(Find(registry, LampGuid)).Intensity = 9.0f;

		// A component taken away, another added
		const Entity sign = Find(registry, SignGuid);
		registry.Remove<PointLightComponent>(sign);
		registry.Add<Nyx::TimeOfDayComponent>(sign, Nyx::TimeOfDayComponent{ .Hours = 7.0f });

		// Made at run time, with values of its own
		const Entity made = Add(registry, MadeGuid, "Made", glm::vec3(1.0f, 2.0f, 3.0f));
		registry.Add<PointLightComponent>(made, PointLightComponent{ .Intensity = 12.0f });

		// A helper without a guid, which its system makes again
		const Entity helper = registry.CreateEntity();
		registry.Add<TransformComponent>(helper);

		// Destroyed, by a player's command at the next step
		world.GetCommands().Add(Nyx::DestroyEntityCommand{ DoomedGuid });
	}

	void TestARunningWorldComesBack()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteScene(folder.Path);
		const std::filesystem::path save = folder.Path / "Run.nyxsave";

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scene, base);
		RunSteps(running, 0, 30);
		ChangeTheWorld(running);
		const WorldSaveClock clock = RunSteps(running, 30, 30);
		const Registry& ran = running.GetRegistry();
		const float crateHeight = ran.Get<TransformComponent>(Find(ran, CrateGuid)).Position.y;
		Require(crateHeight < 49.0f, "The crate should have fallen before it is saved");
		Require(!Nyx::FindEntityByGuid(ran, DoomedGuid), "The command should have destroyed its entity");

		Require(Nyx::SaveWorldToFile(base, ran, clock, save), "The world should be saved");

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		MustLoad(save, loaded.GetRegistry(), loadedBase, loadedClock);
		Registry& world = loaded.GetRegistry();

		RequireSameWorld(ran, world, "Loaded");
		Require(world.Get<TransformComponent>(Find(world, CrateGuid)).Position.y == crateHeight, "The crate should be where it fell to");
		Require(world.Get<PointLightComponent>(Find(world, LampGuid)).Intensity == 9.0f, "The lamp's changed value should come back");
		const Entity sign = Find(world, SignGuid);
		Require(!world.Has<PointLightComponent>(sign), "The sign's light was taken away");
		Require(world.Has<Nyx::TimeOfDayComponent>(sign) && world.Get<Nyx::TimeOfDayComponent>(sign).Hours == 7.0f, "The sign's new component should come back");
		Require(world.Get<PointLightComponent>(Find(world, MadeGuid)).Intensity == 12.0f, "What was made at run time should come back");
		Require(world.Get<Nyx::NameComponent>(Find(world, MadeGuid)).Name == "Made", "What was made at run time should have its name");
		Require(!Nyx::FindEntityByGuid(world, DoomedGuid), "What was destroyed should stay destroyed");

		size_t withoutGuid = 0;
		world.ForEachEntity([&](Entity entity)
			{
				withoutGuid += world.Has<Nyx::GuidComponent>(entity) ? 0 : 1;
			});
		Require(withoutGuid == 0, "An entity without a guid isn't saved");

		// It runs on from there: its bodies made anew, the crate falling on
		RunSteps(loaded, loadedClock.StepIndex, 10);
		Require(world.Get<TransformComponent>(Find(world, CrateGuid)).Position.y < crateHeight, "The crate should fall on after the load");

		// Saved again, the base still the scene: as far from it as before, and it comes back the same
		const std::filesystem::path again = folder.Path / "Again.nyxsave";
		Require(Nyx::SaveWorldToFile(loadedBase, world, loadedClock, again), "The loaded world should be saved");
		GameWorld second;
		WorldSaveBase secondBase;
		WorldSaveClock secondClock;
		MustLoad(again, second.GetRegistry(), secondBase, secondClock);
		RequireSameWorld(world, second.GetRegistry(), "Loaded a second time");
		Require(!Nyx::FindEntityByGuid(second.GetRegistry(), DoomedGuid), "A second save should still say what was destroyed");
	}

	// A save holds only what differs, so what it doesn't hold follows the scene
	void TestASaveIsTheDifferenceFromItsScene()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scenePath = folder.Path / "Big.nyxscene";
		const std::filesystem::path save = folder.Path / "Few.nyxsave";

		Registry scene;
		const Entity lamp = Add(scene, LampGuid, "Lamp");
		scene.Add<PointLightComponent>(lamp, PointLightComponent{ .Intensity = 3.0f, .Range = 5.0f });
		Add(scene, SignGuid, "Sign");
		Add(scene, DoomedGuid, "Doomed");
		for (uint64_t index = 0; index < 500; ++index)
		{
			Add(scene, EntityGuid{ 1000 + index }, "Plain");
		}
		Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath), "The scene should be saved");

		Registry running;
		WorldSaveBase base;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, running), "The scene should load");
		Require(Nyx::MakeWorldSaveBase(scenePath, running, base), "The world should give a base");

		// Nothing changed: nothing but its header
		Require(Nyx::SaveWorldToFile(base, running, {}, save), "The world should be saved");
		const auto unchangedSize = std::filesystem::file_size(save);
		Require(unchangedSize < std::filesystem::file_size(scenePath) / 20, "An unchanged world should save next to nothing");

		// One property changed: only that, and the name of its component
		running.Get<PointLightComponent>(Find(running, LampGuid)).Intensity = 9.0f;
		running.Get<TransformComponent>(Find(running, SignGuid)).Position.x = 4.0f;
		Require(Nyx::SaveWorldToFile(base, running, {}, save), "The world should be saved");
		Require(std::filesystem::file_size(save) < unchangedSize + 200, "Two changed values should save little more than none");

		// The scene changes under the save: another value of the same component, a value of an entity
		// the save doesn't name, a new entity, one gone
		scene.Get<PointLightComponent>(lamp).Range = 8.0f;
		scene.Get<Nyx::NameComponent>(Find(scene, EntityGuid{ 1000 })).Name = "Renamed";
		Add(scene, EntityGuid{ 5000 }, "New in the scene");
		scene.DestroyEntity(Find(scene, SignGuid));
		Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath), "The changed scene should be saved");

		Registry world;
		WorldSaveBase loadedBase;
		WorldSaveClock clock;
		MustLoad(save, world, loadedBase, clock);
		const PointLightComponent& light = world.Get<PointLightComponent>(Find(world, LampGuid));
		Require(light.Intensity == 9.0f, "The value the world changed should stay");
		Require(light.Range == 8.0f, "The value it didn't change should follow the scene");
		Require(world.Get<Nyx::NameComponent>(Find(world, EntityGuid{ 1000 })).Name == "Renamed", "An entity it didn't change should follow the scene");
		Require(Nyx::FindEntityByGuid(world, EntityGuid{ 5000 }).has_value(), "A new entity of the scene should be there");
		Require(!Nyx::FindEntityByGuid(world, SignGuid), "What the save has of an entity the scene lost should be dropped");
	}

	void TestChildrenStayWhereTheyWereLeft()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scenePath = folder.Path / "Parents.nyxscene";
		const std::filesystem::path save = folder.Path / "Parents.nyxsave";

		constexpr EntityGuid ParentGuid{ 301 };
		constexpr EntityGuid LeftBehindGuid{ 302 };
		constexpr EntityGuid FollowerGuid{ 303 };
		constexpr EntityGuid DetachedGuid{ 304 };
		constexpr EntityGuid AttachedGuid{ 305 };

		Registry scene;
		Add(scene, ParentGuid, "Parent");
		const auto addChild = [&](EntityGuid guid, const char* name, bool bFollows)
		{
			const Entity child = Add(scene, guid, name);
			scene.Add<ParentComponent>(child, ParentComponent{ .Parent = ParentGuid, .LocalPosition = glm::vec3(1.0f, 0.0f, 0.0f), .bFollowInGame = bFollows });
		};
		addChild(LeftBehindGuid, "Left behind", false);
		addChild(FollowerGuid, "Follower", true);
		addChild(DetachedGuid, "Detached", false);
		Add(scene, AttachedGuid, "Attached", glm::vec3(0.0f, 7.0f, 0.0f));
		Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath), "The scene should be saved");

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scenePath, base);
		Registry& ran = running.GetRegistry();
		ran.Get<TransformComponent>(Find(ran, ParentGuid)).Position = glm::vec3(0.0f, 0.0f, 10.0f);
		running.GetCommands().Add(Nyx::DetachEntityCommand{ DetachedGuid });
		running.GetCommands().Add(Nyx::AttachEntityCommand{ AttachedGuid, ParentGuid });
		const WorldSaveClock clock = RunSteps(running, 0, 2);

		const glm::vec3 leftBehind = ran.Get<TransformComponent>(Find(ran, LeftBehindGuid)).Position;
		const glm::vec3 follower = ran.Get<TransformComponent>(Find(ran, FollowerGuid)).Position;
		Require(leftBehind == glm::vec3(1.0f, 0.0f, 0.0f), "A child that doesn't follow should stay where its parent left it");
		Require(follower == glm::vec3(1.0f, 0.0f, 10.0f), "A child that follows should have come along");
		Require(Nyx::SaveWorldToFile(base, ran, clock, save), "The world should be saved");

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		MustLoad(save, loaded.GetRegistry(), loadedBase, loadedClock);
		Registry& world = loaded.GetRegistry();
		Require(world.Get<TransformComponent>(Find(world, LeftBehindGuid)).Position == leftBehind, "The load shouldn't place a child by its parent again");
		Require(!world.Has<ParentComponent>(Find(world, DetachedGuid)), "A child detached should stay detached");
		const Entity attached = Find(world, AttachedGuid);
		Require(world.Has<ParentComponent>(attached) && world.Get<ParentComponent>(attached).Parent == ParentGuid, "An entity attached should stay attached");
		Require(world.Get<TransformComponent>(attached).Position == glm::vec3(0.0f, 7.0f, 0.0f), "An entity attached should stay where it was");

		// The links work on: a follower comes along when its parent moves after the load
		RunSteps(loaded, loadedClock.StepIndex, 1);
		world.Get<TransformComponent>(Find(world, ParentGuid)).Position = glm::vec3(0.0f, 0.0f, 20.0f);
		RunSteps(loaded, loadedClock.StepIndex + 1, 1);
		Require(world.Get<TransformComponent>(Find(world, FollowerGuid)).Position == glm::vec3(1.0f, 0.0f, 20.0f), "A follower should follow on after the load");
		Require(world.Get<TransformComponent>(Find(world, LeftBehindGuid)).Position == leftBehind, "A child that doesn't follow should still stay");
	}

	void TestTheClockGoesOn()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scenePath = folder.Path / "Day.nyxscene";
		const std::filesystem::path save = folder.Path / "Day.nyxsave";

		constexpr EntityGuid DayGuid{ 401 };
		Registry scene;
		const Entity day = Add(scene, DayGuid, "Day");
		scene.Add<Nyx::TimeOfDayComponent>(day, Nyx::TimeOfDayComponent{ .Hours = 6.0f, .bTimeRuns = true, .DayLengthSeconds = 240.0f });
		Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath), "The scene should be saved");

		// A world that runs on without a load, to compare with: 10 s are an hour
		GameWorld whole;
		WorldSaveBase wholeBase;
		StartWorld(whole, scenePath, wholeBase);
		RunSteps(whole, 0, 1200);
		const float wholeHours = whole.GetRegistry().Get<Nyx::TimeOfDayComponent>(Find(whole.GetRegistry(), DayGuid)).Hours;
		Require(std::abs(wholeHours - 8.0f) < 0.01f, "20 s of world time should be two hours");

		// One saved at 15 s, loaded and run on for the rest
		GameWorld first;
		WorldSaveBase base;
		StartWorld(first, scenePath, base);
		const WorldSaveClock clock = RunSteps(first, 0, 900);
		Require(Nyx::SaveWorldToFile(base, first.GetRegistry(), clock, save), "The world should be saved");

		GameWorld second;
		WorldSaveBase loadedBase;
		WorldSaveClock loaded;
		MustLoad(save, second.GetRegistry(), loadedBase, loaded);
		Require(loaded.StepIndex == 900 && loaded.WorldTimeSeconds == clock.WorldTimeSeconds && loaded.StepSeconds == Step, "The clock should come back to the bit");

		// The real clock, set from what came back, gives the steps to run on with
		Nyx::FixedStepClock real;
		real.SetStepSeconds(loaded.StepSeconds);
		real.SetTime(loaded.StepIndex, loaded.WorldTimeSeconds);
		for (int index = 0; index < 300; ++index)
		{
			const uint32_t steps = real.Advance(Step);
			for (uint32_t step = 0; step < steps; ++step)
			{
				second.Step(real.GetFrameStep(step));
			}
		}
		Require(real.GetStepIndex() == 1200, "The clock should have run on to the end");
		const float hours = second.GetRegistry().Get<Nyx::TimeOfDayComponent>(Find(second.GetRegistry(), DayGuid)).Hours;
		Require(std::abs(hours - wholeHours) < 0.001f, "The time of day should go on from where it was saved: " + std::to_string(hours) + " against " + std::to_string(wholeHours));
	}

	// The snapshot is all a save needs: made on another thread, it doesn't see the world change after
	void TestASaveIsMadeFromItsSnapshot()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteScene(folder.Path);
		const std::filesystem::path save = folder.Path / "Worker.nyxsave";

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scene, base);
		Registry& world = running.GetRegistry();
		world.Get<PointLightComponent>(Find(world, LampGuid)).Intensity = 9.0f;

		Nyx::WorldSnapshot snapshot;
		Require(Nyx::TakeWorldSnapshot(world, snapshot), "The world should give a snapshot");

		bool bWritten = false;
		std::thread worker([&]
			{
				Nyx::BinaryWriter out;
				bWritten = Nyx::WriteWorldSave(base, snapshot, WorldSaveClock{}, out) && out.SaveToFile(save);
			});
		world.Get<PointLightComponent>(Find(world, LampGuid)).Intensity = 20.0f;
		Add(world, MadeGuid, "Made too late");
		worker.join();
		Require(bWritten, "The save should be made on the other thread");

		Registry loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock clock;
		MustLoad(save, loaded, loadedBase, clock);
		Require(loaded.Get<PointLightComponent>(Find(loaded, LampGuid)).Intensity == 9.0f, "The save should have the values of its snapshot");
		Require(!Nyx::FindEntityByGuid(loaded, MadeGuid), "The save shouldn't have what came after its snapshot");
	}

	std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
	{
		std::vector<std::byte> bytes(std::filesystem::file_size(path));
		std::ifstream file(path, std::ios::binary);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(file.good(), "The file should be read");
		return bytes;
	}

	void WriteFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		Nyx::BinaryWriter writer;
		writer.WriteBytes(bytes.data(), bytes.size());
		Require(writer.SaveToFile(path), "The file should be written");
	}

	// A world that a failed load must leave as it is
	void RequireRefused(const std::filesystem::path& path, std::string_view expected, const std::string& what)
	{
		Registry world;
		const Entity kept = Add(world, EntityGuid{ 777 }, "Kept");
		WorldSaveBase base;
		WorldSaveClock clock;
		std::string error;
		Require(!Nyx::LoadWorldSave(path, world, {}, base, clock, error), what + " should be refused");
		Require(error.find(expected) != std::string::npos, what + " should say why, not: " + error);
		Require(world.IsAlive(kept) && Nyx::FindEntityByGuid(world, EntityGuid{ 777 }), what + " should leave the world as it was");
	}

	void TestOldNewAndDamagedSaves()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteScene(folder.Path);
		const std::filesystem::path save = folder.Path / "Good.nyxsave";

		Registry running;
		WorldSaveBase base;
		Require(Nyx::SceneSerializer::LoadFromFile(scene, running), "The scene should load");
		Require(Nyx::MakeWorldSaveBase(scene, running, base), "The world should give a base");
		running.Get<PointLightComponent>(Find(running, LampGuid)).Intensity = 9.0f;
		Require(Nyx::SaveWorldToFile(base, running, {}, save), "The world should be saved");
		const std::vector<std::byte> good = ReadBytes(save);
		const std::filesystem::path other = folder.Path / "Other.nyxsave";

		// A section of a later build
		{
			Nyx::BinaryWriter extended;
			extended.WriteBytes(good.data(), good.size());
			Nyx::BinaryWriter later;
			later.WriteUInt32(1234);
			extended.WriteString("Later Section");
			extended.WriteBlock(later);
			Require(extended.SaveToFile(other), "The file should be written");

			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			std::string error;
			Require(Nyx::LoadWorldSave(other, world, {}, loadedBase, clock, error), "A section this build doesn't know should be skipped, but it " + error);
			Require(world.Get<PointLightComponent>(Find(world, LampGuid)).Intensity == 9.0f, "What it knows should be read");
		}

		// A record with component types this build doesn't know, in an otherwise unchanged save: the
		// file's end is its Changed and Made sections, both empty, and its Changed takes their place
		{
			Nyx::BinaryWriter unchanged;
			Require(Nyx::WriteWorldSave(base, base.Entities, {}, unchanged), "An unchanged world should be written");

			Nyx::BinaryWriter noRecords;
			noRecords.WriteUInt32(0);
			Nyx::BinaryWriter emptyTail;
			emptyTail.WriteString("Changed");
			emptyTail.WriteBlock(noRecords);
			emptyTail.WriteString("Made");
			emptyTail.WriteBlock(noRecords);
			emptyTail.WriteString("Instances");
			emptyTail.WriteBlock(noRecords);
			emptyTail.WriteString("Motions");
			emptyTail.WriteBlock(noRecords);
			emptyTail.WriteString("Terrains");
			emptyTail.WriteBlock(noRecords);

			Nyx::BinaryWriter record;
			record.WriteUInt64(PlainGuid.Value);
			record.WriteUInt32(1);
			record.WriteString("Game::GoneFromThisBuild");
			record.WriteUInt32(1);
			record.WriteString("Game::NotInThisBuild");
			Nyx::BinaryWriter unknownProperties;
			unknownProperties.WriteUInt16(0);
			record.WriteBlock(unknownProperties);
			Nyx::BinaryWriter changed;
			changed.WriteUInt32(1);
			changed.WriteBlock(record);

			Nyx::BinaryWriter edited;
			edited.WriteBytes(unchanged.GetBytes().data(), unchanged.GetBytes().size() - emptyTail.GetBytes().size());
			edited.WriteString("Changed");
			edited.WriteBlock(changed);
			Require(edited.SaveToFile(other), "The file should be written");

			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			std::string error;
			Require(Nyx::LoadWorldSave(other, world, {}, loadedBase, clock, error), "Types this build doesn't know should be skipped, but it " + error);
			Require(Nyx::FindEntityByGuid(world, PlainGuid).has_value(), "The entity should be there without them");
		}

		// A save of the first version, which had neither instances nor motions
		{
			Nyx::BinaryWriter first;
			Nyx::BinaryWriter noRecords;
			noRecords.WriteUInt32(0);
			Nyx::BinaryWriter newSections;
			newSections.WriteString("Instances");
			newSections.WriteBlock(noRecords);
			newSections.WriteString("Motions");
			newSections.WriteBlock(noRecords);
			newSections.WriteString("Terrains");
			newSections.WriteBlock(noRecords);
			first.WriteBytes(good.data(), good.size() - newSections.GetBytes().size());
			std::vector<std::byte> bytes = first.GetBytes();
			const uint32_t version = 1;
			std::memcpy(bytes.data() + sizeof(uint32_t), &version, sizeof(version));
			WriteFile(other, bytes);

			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			std::string error;
			Require(Nyx::LoadWorldSave(other, world, {}, loadedBase, clock, error), "A save of the first version should load, but it " + error);
			Require(world.Get<PointLightComponent>(Find(world, LampGuid)).Intensity == 9.0f, "A save of the first version should have its values");
		}

		// A version of a later build
		{
			std::vector<std::byte> later = good;
			const uint32_t version = Nyx::WorldSaveVersion + 1;
			std::memcpy(later.data() + sizeof(uint32_t), &version, sizeof(version));
			WriteFile(other, later);
			RequireRefused(other, "version", "A save of a later version");
		}

		// Not a save at all, and one cut off at every length up to whole
		{
			std::vector<std::byte> notSave = good;
			notSave[0] = std::byte{ 0 };
			WriteFile(other, notSave);
			RequireRefused(other, "is not a save", "A file that isn't a save");

			for (size_t length : { size_t{ 6 }, size_t{ 12 }, good.size() / 2, good.size() - 1 })
			{
				WriteFile(other, std::span<const std::byte>(good).first(length));
				RequireRefused(other, length < 8 ? "is not a save" : "ends early", "A save cut off at " + std::to_string(length) + " bytes");
			}
		}

		// Its scene gone
		{
			std::filesystem::remove(scene);
			RequireRefused(save, "World.nyxscene", "A save without its scene");
		}
	}

	constexpr const char* LampPrefab = "Prefabs/Lamp.nyxprefab";
	constexpr EntityGuid PostLocal{ 0x10 };
	constexpr EntityGuid ArmLocal{ 0x20 };
	constexpr EntityGuid BulbLocal{ 0x30 };
	constexpr EntityGuid NewLocal{ 0x40 };

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
		PrefabAsset* ResolvePrefab(const std::string& path) override
		{
			const auto found = Prefabs.find(path);
			return found != Prefabs.end() ? found->second.get() : nullptr;
		}

		std::map<std::string, std::unique_ptr<PrefabAsset>> Prefabs;
	};

	struct LampChoices
	{
		float Range = 5.0f;
		// A bulb no longer there, and a new part below the post
		bool bLater = false;
		// The bulb on the post instead of the arm
		bool bBulbOnPost = false;
	};

	// A post with a light, an arm that follows it with a light, a bulb on the arm that doesn't follow
	std::unique_ptr<PrefabAsset> MakeLamp(const std::filesystem::path& folder, const LampChoices& choices = {})
	{
		Registry world;
		const auto light = [&](Entity entity)
		{
			world.Add<PointLightComponent>(entity, PointLightComponent{ .Intensity = 1.0f, .Range = choices.Range });
		};
		const Entity post = Add(world, PostLocal, "Post");
		light(post);
		const Entity arm = Add(world, ArmLocal, "Arm");
		light(arm);
		world.Add<ParentComponent>(arm, ParentComponent{ .Parent = PostLocal, .LocalPosition = glm::vec3(0.0f, 3.0f, 0.0f), .bFollowInGame = true });
		if (!choices.bLater)
		{
			const Entity bulb = Add(world, BulbLocal, "Bulb");
			light(bulb);
			world.Add<ParentComponent>(bulb, ParentComponent{ .Parent = choices.bBulbOnPost ? PostLocal : ArmLocal, .LocalPosition = glm::vec3(1.0f, 0.0f, 0.0f) });
		}
		else
		{
			const Entity added = Add(world, NewLocal, "New");
			world.Add<ParentComponent>(added, ParentComponent{ .Parent = PostLocal, .LocalPosition = glm::vec3(0.0f, 0.0f, 2.0f) });
		}

		const std::filesystem::path path = folder / "Lamp.nyxprefab";
		Require(PrefabAsset::SaveToFile(world, post, path), "The prefab should be saved");
		auto prefab = std::make_unique<PrefabAsset>();
		std::string error;
		Require(prefab->LoadFromFile(path, error), "The prefab should load, but it " + error);
		return prefab;
	}

	void Spawn(GameWorld& world, EntityGuid guid, const glm::vec3& position)
	{
		world.GetCommands().Add(Nyx::SpawnPrefabCommand{ .Prefab = Nyx::AssetReference{ .Type = "Prefab", .Path = LampPrefab }, .Guid = guid, .Position = position });
	}

	float LightOf(const Registry& world, EntityGuid guid)
	{
		return world.Get<PointLightComponent>(Find(world, guid)).Intensity;
	}

	void MustLoadInto(GameWorld& world, TestPrefabs& prefabs, const std::filesystem::path& path, WorldSaveBase& outBase, WorldSaveClock& outClock)
	{
		world.SetAssetResolver(&prefabs);
		std::string error;
		Require(world.LoadSave(path, Nyx::ScenePostLoadContext{ .AssetResolver = &prefabs }, outBase, outClock, error), "The save should load, but it " + error);
	}

	void TestInstancesMadeAtRunTimeComeBack()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteScene(folder.Path);
		const std::filesystem::path save = folder.Path / "Instances.nyxsave";
		const std::filesystem::path whole = folder.Path / "Whole.nyxsave";

		TestPrefabs prefabs;
		prefabs.Prefabs[LampPrefab] = MakeLamp(folder.Path);

		constexpr EntityGuid ChangedLamp{ 501 };
		constexpr EntityGuid CutLamp{ 502 };
		constexpr EntityGuid GoneLamp{ 503 };
		constexpr EntityGuid PlainLamp{ 504 };
		constexpr EntityGuid HangingGuid{ 505 };
		const auto part = [](EntityGuid instance, EntityGuid local)
		{
			return Nyx::GetPrefabPartGuid(instance, local);
		};

		GameWorld running;
		running.SetAssetResolver(&prefabs);
		WorldSaveBase base;
		StartWorld(running, scene, base);
		Spawn(running, ChangedLamp, glm::vec3(10.0f, 0.0f, 0.0f));
		Spawn(running, CutLamp, glm::vec3(20.0f, 0.0f, 0.0f));
		Spawn(running, GoneLamp, glm::vec3(30.0f, 0.0f, 0.0f));
		Spawn(running, PlainLamp, glm::vec3(40.0f, 0.0f, 0.0f));
		RunSteps(running, 0, 1);
		Registry& ran = running.GetRegistry();

		// One's values changed, and a part left somewhere else
		ran.Get<PointLightComponent>(Find(ran, ChangedLamp)).Intensity = 9.0f;
		ran.Get<PointLightComponent>(Find(ran, part(ChangedLamp, ArmLocal))).Intensity = 7.0f;
		ran.Get<TransformComponent>(Find(ran, part(ChangedLamp, BulbLocal))).Position = glm::vec3(99.0f, 0.0f, 0.0f);
		// One with a part gone, a part's light taken away and something else on it instead; the rest
		// of it hung on the scene's lamp
		running.GetCommands().Add(Nyx::DestroyEntityCommand{ part(CutLamp, BulbLocal) });
		const Entity cutArm = Find(ran, part(CutLamp, ArmLocal));
		ran.Remove<PointLightComponent>(cutArm);
		ran.Add<Nyx::TimeOfDayComponent>(cutArm, Nyx::TimeOfDayComponent{ .Hours = 7.0f });
		running.GetCommands().Add(Nyx::AttachEntityCommand{ CutLamp, LampGuid });
		// A part hung on something else
		running.GetCommands().Add(Nyx::AttachEntityCommand{ part(ChangedLamp, BulbLocal), LampGuid });
		// One gone with a part kept
		running.GetCommands().Add(Nyx::DetachEntityCommand{ part(GoneLamp, BulbLocal) });
		// A thing made at run time on a part of one
		const Entity hanging = Add(ran, HangingGuid, "Hanging", glm::vec3(10.0f, 2.0f, 0.0f));
		ran.Add<ParentComponent>(hanging, ParentComponent{ .Parent = part(ChangedLamp, ArmLocal), .LocalPosition = glm::vec3(0.0f, -1.0f, 0.0f), .bFollowInGame = true });
		RunSteps(running, 1, 1);
		ran.Get<PointLightComponent>(Find(ran, part(GoneLamp, BulbLocal))).Intensity = 6.0f;
		running.GetCommands().Add(Nyx::DestroyEntityCommand{ GoneLamp });
		const WorldSaveClock clock = RunSteps(running, 2, 2);
		Require(!Nyx::FindEntityByGuid(ran, GoneLamp) && Nyx::FindEntityByGuid(ran, part(GoneLamp, BulbLocal)), "A part detached should outlive its instance");

		Require(running.SaveToFile(base, clock, save), "The world should be saved");
		Require(Nyx::SaveWorldToFile(base, ran, clock, whole), "The world should be saved without its prefabs");
		Require(std::filesystem::file_size(save) < std::filesystem::file_size(whole), "An instance should save as less than all it has");

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		MustLoadInto(loaded, prefabs, save, loadedBase, loadedClock);
		Registry& world = loaded.GetRegistry();
		RequireSameWorld(ran, world, "Loaded with instances");
		Require(LightOf(world, ChangedLamp) == 9.0f && LightOf(world, part(ChangedLamp, ArmLocal)) == 7.0f, "An instance's changed values should come back");
		Require(world.Get<TransformComponent>(Find(world, part(ChangedLamp, BulbLocal))).Position == glm::vec3(99.0f, 0.0f, 0.0f), "A part should stay where it was left");
		Require(!Nyx::FindEntityByGuid(world, part(CutLamp, BulbLocal)), "A part that was gone shouldn't be made again");
		Require(!world.Has<PointLightComponent>(Find(world, part(CutLamp, ArmLocal))), "A part's light that was taken away should stay away");
		Require(world.Has<Nyx::TimeOfDayComponent>(Find(world, part(CutLamp, ArmLocal))), "What a part got should come back");
		Require(world.Get<ParentComponent>(Find(world, CutLamp)).Parent == LampGuid, "An instance's parent should come back");
		Require(world.Get<ParentComponent>(Find(world, part(ChangedLamp, BulbLocal))).Parent == LampGuid, "A part's new parent should come back");
		Require(!Nyx::FindEntityByGuid(world, GoneLamp) && !Nyx::FindEntityByGuid(world, part(GoneLamp, ArmLocal)), "What was destroyed shouldn't be made again");
		const Entity kept = Find(world, part(GoneLamp, BulbLocal));
		Require(LightOf(world, part(GoneLamp, BulbLocal)) == 6.0f && !world.Has<ParentComponent>(kept), "A part that outlived its instance should come back");

		// The links work on: the arm follows its post, and what hangs on it follows on
		RunSteps(loaded, loadedClock.StepIndex, 1);
		world.Get<TransformComponent>(Find(world, ChangedLamp)).Position = glm::vec3(10.0f, 0.0f, 5.0f);
		RunSteps(loaded, loadedClock.StepIndex + 1, 2);
		Require(world.Get<TransformComponent>(Find(world, part(ChangedLamp, ArmLocal))).Position == glm::vec3(10.0f, 3.0f, 5.0f), "A part that follows should follow on after the load");
		Require(world.Get<TransformComponent>(Find(world, HangingGuid)).Position == glm::vec3(10.0f, 2.0f, 5.0f), "What was made on a part should follow it on");
		Require(world.Get<TransformComponent>(Find(world, part(ChangedLamp, BulbLocal))).Position == glm::vec3(99.0f, 0.0f, 0.0f), "A part that doesn't follow should stay");

		// Saved and loaded again, its instances still what differs from their prefabs
		const std::filesystem::path again = folder.Path / "Again.nyxsave";
		Require(loaded.SaveToFile(loadedBase, loadedClock, again), "The loaded world should be saved");
		GameWorld second;
		WorldSaveBase secondBase;
		WorldSaveClock secondClock;
		MustLoadInto(second, prefabs, again, secondBase, secondClock);
		RequireSameWorld(world, second.GetRegistry(), "Loaded with instances a second time");

		// The prefab changes under the save: a new value where the world changed none, a part gone, a
		// new one, made where its instance is
		prefabs.Prefabs[LampPrefab] = MakeLamp(folder.Path, LampChoices{ .Range = 8.0f, .bLater = true });
		GameWorld later;
		WorldSaveBase laterBase;
		WorldSaveClock laterClock;
		MustLoadInto(later, prefabs, save, laterBase, laterClock);
		const Registry& laterWorld = later.GetRegistry();
		Require(LightOf(laterWorld, ChangedLamp) == 9.0f, "The value the world changed should stay");
		Require(laterWorld.Get<PointLightComponent>(Find(laterWorld, ChangedLamp)).Range == 8.0f, "The value it didn't change should follow the prefab");
		Require(laterWorld.Get<PointLightComponent>(Find(laterWorld, part(PlainLamp, ArmLocal))).Range == 8.0f, "A part's value it didn't change should follow the prefab");
		Require(!Nyx::FindEntityByGuid(laterWorld, part(PlainLamp, BulbLocal)), "What the save has of a part the prefab lost should be dropped");
		Require(laterWorld.Get<TransformComponent>(Find(laterWorld, part(PlainLamp, NewLocal))).Position == glm::vec3(40.0f, 0.0f, 2.0f), "A part the prefab gained should be made where its instance is");

		// A part's parent changed in the prefab: it follows, in an instance that didn't change it
		prefabs.Prefabs[LampPrefab] = MakeLamp(folder.Path, LampChoices{ .bBulbOnPost = true });
		GameWorld moved;
		WorldSaveBase movedBase;
		WorldSaveClock movedClock;
		MustLoadInto(moved, prefabs, save, movedBase, movedClock);
		const Registry& movedWorld = moved.GetRegistry();
		Require(movedWorld.Get<ParentComponent>(Find(movedWorld, part(PlainLamp, BulbLocal))).Parent == PlainLamp, "A part's parent the world didn't change should follow the prefab");
		Require(movedWorld.Get<ParentComponent>(Find(movedWorld, part(ChangedLamp, BulbLocal))).Parent == LampGuid, "A part's parent the world changed should stay");

		// The prefab gone: the root is all that is left, with its place
		prefabs.Prefabs.clear();
		GameWorld without;
		WorldSaveBase withoutBase;
		WorldSaveClock withoutClock;
		MustLoadInto(without, prefabs, save, withoutBase, withoutClock);
		const Registry& withoutWorld = without.GetRegistry();
		Require(withoutWorld.Get<TransformComponent>(Find(withoutWorld, PlainLamp)).Position == glm::vec3(40.0f, 0.0f, 0.0f), "An instance without its prefab should stay where it was");
		Require(!Nyx::FindEntityByGuid(withoutWorld, part(PlainLamp, ArmLocal)), "An instance without its prefab should have no parts");
	}

	// An instance of the scene is a part of the scene: compared with it like any of its entities
	void TestInstancesOfTheSceneComeBack()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scenePath = folder.Path / "Lamps.nyxscene";
		const std::filesystem::path save = folder.Path / "Lamps.nyxsave";

		TestPrefabs prefabs;
		prefabs.Prefabs[LampPrefab] = MakeLamp(folder.Path);
		constexpr EntityGuid ThereGuid{ 601 };
		{
			Registry scene;
			const Entity root = Add(scene, ThereGuid, "There", glm::vec3(1.0f, 0.0f, 0.0f));
			scene.Add<PrefabInstanceComponent>(root, PrefabInstanceComponent{ .Prefab = Nyx::AssetReference{ .Type = "Prefab", .Path = LampPrefab } });
			Nyx::ScenePostLoadContext context{ .AssetResolver = &prefabs };
			Nyx::ApplyPrefab(scene, root, *prefabs.Prefabs[LampPrefab], context);
			Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath, nullptr, &prefabs), "The scene should be saved");
		}

		GameWorld running;
		running.SetAssetResolver(&prefabs);
		Registry& ran = running.GetRegistry();
		WorldSaveBase base;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, ran, Nyx::ScenePostLoadContext{ .AssetResolver = &prefabs }), "The scene should load");
		Require(Nyx::MakeWorldSaveBase(scenePath, ran, base), "The world should give a base");

		const EntityGuid arm = Nyx::GetPrefabPartGuid(ThereGuid, ArmLocal);
		const EntityGuid bulb = Nyx::GetPrefabPartGuid(ThereGuid, BulbLocal);
		ran.Get<PointLightComponent>(Find(ran, arm)).Intensity = 7.0f;
		running.GetCommands().Add(Nyx::DestroyEntityCommand{ bulb });
		const WorldSaveClock clock = RunSteps(running, 0, 1);
		Require(running.SaveToFile(base, clock, save), "The world should be saved");

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		MustLoadInto(loaded, prefabs, save, loadedBase, loadedClock);
		RequireSameWorld(ran, loaded.GetRegistry(), "Loaded with an instance of the scene");
		Require(LightOf(loaded.GetRegistry(), arm) == 7.0f, "A part's changed value should come back");
		Require(!Nyx::FindEntityByGuid(loaded.GetRegistry(), bulb), "A part destroyed should stay destroyed");
	}

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < 0.001f;
	}

	// A world that runs on without a load against one saved halfway, loaded and run on: as long as
	// nothing but these move, they should end up in the same places
	void TestBodiesGoOnMoving()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scenePath = folder.Path / "Moving.nyxscene";
		const std::filesystem::path save = folder.Path / "Moving.nyxsave";

		constexpr EntityGuid FlyerGuid{ 701 };
		constexpr EntityGuid FirstGuid{ 702 };
		constexpr EntityGuid SecondGuid{ 703 };
		constexpr EntityGuid WalkerGuid{ 704 };
		constexpr EntityGuid StillGuid{ 705 };
		constexpr EntityGuid HeldGuid{ 706 };

		Registry scene;
		const auto addBox = [&](EntityGuid guid, const char* name, const glm::vec3& position)
		{
			const Entity box = Add(scene, guid, name, position);
			scene.Add<Nyx::BoxColliderComponent>(box);
			scene.Add<Nyx::RigidBodyComponent>(box);
			return box;
		};
		addBox(FlyerGuid, "Flyer", glm::vec3(0.0f, 100.0f, 0.0f));
		addBox(FirstGuid, "First", glm::vec3(50.0f, 100.0f, 0.0f));
		addBox(SecondGuid, "Second", glm::vec3(53.0f, 100.0f, 0.0f));
		scene.Add<Nyx::CharacterComponent>(Add(scene, WalkerGuid, "Walker", glm::vec3(100.0f, 100.0f, 0.0f)));
		const Entity still = addBox(StillGuid, "Still", glm::vec3(150.0f, 100.0f, 0.0f));
		scene.Get<Nyx::RigidBodyComponent>(still).GravityFactor = 0.0f;
		const Entity held = addBox(HeldGuid, "Held", glm::vec3(200.0f, 100.0f, 0.0f));
		scene.Get<Nyx::RigidBodyComponent>(held).bKinematic = true;
		Require(Nyx::SceneSerializer::SaveToFile(scene, scenePath), "The scene should be saved");

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scenePath, base);
		const Registry& ran = running.GetRegistry();

		// A group is a run-time component, which whatever made it makes again
		const auto group = [](Registry& world, EntityGuid guid)
		{
			world.Add<Nyx::BodyGroupComponent>(Find(world, guid), Nyx::BodyGroupComponent{ .Group = 5 });
		};
		group(running.GetRegistry(), FirstGuid);
		group(running.GetRegistry(), SecondGuid);
		RunSteps(running, 0, 1);
		Require(running.GetBodies().GetGroupCount() == 1, "The two should be one body");

		// Thrown and spinning
		Nyx::PhysicsWorld& physics = running.GetPhysics();
		const Nyx::BodyHandle flyer = running.GetBodies().FindBody(Find(ran, FlyerGuid));
		physics.SetLinearVelocity(flyer, glm::vec3(3.0f, 4.0f, 1.0f));
		physics.SetAngularVelocity(flyer, glm::vec3(0.0f, 2.0f, 1.0f));
		const Nyx::BodyHandle both = running.GetBodies().FindBody(Find(ran, FirstGuid));
		physics.SetLinearVelocity(both, glm::vec3(0.0f, 2.0f, 2.0f));
		physics.SetAngularVelocity(both, glm::vec3(1.0f, 3.0f, 0.0f));

		const WorldSaveClock clock = RunSteps(running, 1, 30);
		Nyx::WorldSnapshot snapshot;
		Require(running.TakeSaveSnapshot(snapshot), "The world should give a snapshot");
		Require(snapshot.Motions.size() == 4, "Only what moves, kinematic bodies aside, should have a motion, not " + std::to_string(snapshot.Motions.size()));
		Require(running.SaveToFile(base, clock, save), "The world should be saved");
		RunSteps(running, clock.StepIndex, 60);

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		std::string error;
		Require(loaded.LoadSave(save, {}, loadedBase, loadedClock, error), "The save should load, but it " + error);
		group(loaded.GetRegistry(), FirstGuid);
		group(loaded.GetRegistry(), SecondGuid);
		RunSteps(loaded, loadedClock.StepIndex, 60);
		Require(loaded.GetBodies().GetGroupCount() == 1, "The two should be one body again");
		const Registry& world = loaded.GetRegistry();

		for (const EntityGuid guid : { FlyerGuid, FirstGuid, SecondGuid, WalkerGuid, StillGuid, HeldGuid })
		{
			const TransformComponent& kept = ran.Get<TransformComponent>(Find(ran, guid));
			const TransformComponent& came = world.Get<TransformComponent>(Find(world, guid));
			Require(Near(kept.Position, came.Position), Nyx::GuidToText(guid) + " should go on as it went: at x " + std::to_string(came.Position.x) + " y " + std::to_string(came.Position.y) +
				", not x " + std::to_string(kept.Position.x) + " y " + std::to_string(kept.Position.y));
			Require(std::abs(glm::dot(kept.Rotation, came.Rotation)) > 0.99999f, Nyx::GuidToText(guid) + " should go on turning as it turned");
		}

		// A loaded world's bodies are saved as fast as they go, too
		Nyx::WorldSnapshot again;
		Require(loaded.TakeSaveSnapshot(again) && again.Motions.size() == 4, "A loaded world's bodies should have their motions, too");
	}

	constexpr EntityGuid GroundGuid{ 801 };
	constexpr uint64_t SceneEdits = 6;

	// A hole somewhere near the origin, the nth a little along from the last: all near one brick
	Nyx::SdfTerrainEdit Hole(int n)
	{
		Nyx::SdfTerrainEdit edit;
		edit.Mode = Nyx::ESdfTerrainEditMode::Subtract;
		edit.Position = glm::vec3(1.0f + 0.3f * float(n), 0.0f, 1.0f + 0.2f * float(n % 5));
		edit.Radius = 1.5f;
		return edit;
	}

	std::filesystem::path WriteGround(const std::filesystem::path& folder, int extraSceneEdits = 0, uint32_t seed = 1)
	{
		Registry scene;
		const Entity ground = Add(scene, GroundGuid, "Ground");
		Nyx::SdfTerrainComponent terrain;
		terrain.Seed = seed;
		terrain.BakeAfter = 4;
		for (int n = 0; n < int(SceneEdits) + extraSceneEdits; ++n)
		{
			Nyx::AppendTerrainEdit(terrain, Hole(100 + n));
		}
		scene.Add<Nyx::SdfTerrainComponent>(ground, std::move(terrain));
		const std::filesystem::path path = folder / "Ground.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(scene, path), "The scene should be saved");
		return path;
	}

	const Nyx::SdfTerrainComponent& GroundOf(const Registry& world)
	{
		return world.Get<Nyx::SdfTerrainComponent>(Find(world, GroundGuid));
	}

	void Dig(GameWorld& world, int from, int count)
	{
		for (int n = from; n < from + count; ++n)
		{
			world.GetCommands().Add(Nyx::SdfTerrainEditCommand{ .Terrain = GroundGuid, .Edit = Hole(n) });
		}
	}

	size_t CountBakes(const Nyx::SdfTerrainComponent& terrain, bool bWithValues)
	{
		return size_t(std::count_if(terrain.Edits.begin(), terrain.Edits.end(),
			[&](const Nyx::SdfTerrainEdit& edit)
			{
				return edit.Kind == Nyx::ESdfTerrainEditKind::Bake && (!bWithValues || edit.Baked);
			}));
	}

	// The terrain's values around the holes, for comparing to the bit
	std::vector<float> SampleGround(const Nyx::SdfTerrainComponent& terrain)
	{
		std::vector<float> values;
		for (int x = -2; x < 10; ++x)
		{
			for (int z = -2; z < 6; ++z)
			{
				values.push_back(Nyx::SampleTerrainValue(terrain, glm::vec3(float(x) + 0.25f, -0.5f, float(z) + 0.25f)));
			}
		}
		return values;
	}

	void TestTerrainEditsComeBack()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteGround(folder.Path);
		const std::filesystem::path save = folder.Path / "Dug.nyxsave";

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scene, base);
		const Registry& ran = running.GetRegistry();

		// Nothing dug: the scene's edits aren't saved over again
		Require(running.SaveToFile(base, RunSteps(running, 0, 1), save), "The world should be saved");
		Require(std::filesystem::file_size(save) < 400, "A terrain with only its scene's edits should save nothing of them, not " + std::to_string(std::filesystem::file_size(save)) + " bytes");
		const std::vector<float> asInTheScene = SampleGround(GroundOf(ran));

		// Dug, in steps, so it bakes
		for (uint64_t step = 1; step < 11; ++step)
		{
			Dig(running, int(step), 2);
			RunSteps(running, step, 1);
		}
		const WorldSaveClock clock{ 11, 11.0 * Step, Step };
		const size_t bakes = CountBakes(GroundOf(ran), false);
		Require(bakes > 0, "Digging in one place should have baked it");
		Require(running.SaveToFile(base, clock, save), "The world should be saved");

		GameWorld loaded;
		WorldSaveBase loadedBase;
		WorldSaveClock loadedClock;
		std::string error;
		Require(loaded.LoadSave(save, {}, loadedBase, loadedClock, error), "The save should load, but it " + error);
		const Nyx::SdfTerrainComponent& came = GroundOf(loaded.GetRegistry());
		Require(Nyx::HashTerrainEdits(came) == Nyx::HashTerrainEdits(GroundOf(ran)), "The edits should come back as they were");
		Require(Nyx::GetTerrainRevision(came) == Nyx::GetTerrainRevision(GroundOf(ran)), "The terrain's revision should come back");
		Require(CountBakes(came, true) >= bakes, "The bakes should come back with their values");
		Require(SampleGround(came) == SampleGround(GroundOf(ran)), "The ground should be the same to the bit");
		Require(Nyx::HashTerrainRegions(came) == Nyx::HashTerrainRegions(GroundOf(ran)), "The regions should hash the same");
		RequireSameWorld(ran, loaded.GetRegistry(), "Loaded with a dug terrain");

		// Saved again: still only what is after the scene's
		const std::filesystem::path again = folder.Path / "Again.nyxsave";
		Require(loaded.SaveToFile(loadedBase, loadedClock, again), "The loaded world should be saved");
		Require(std::filesystem::file_size(again) == std::filesystem::file_size(save), "A load saved again should be no bigger");

		// Dug on, the bricks count on: the same bakes at the same revisions as without the load
		for (uint64_t step = 0; step < 10; ++step)
		{
			Dig(running, 50 + int(step), 2);
			RunSteps(running, clock.StepIndex + step, 1);
			Dig(loaded, 50 + int(step), 2);
			RunSteps(loaded, clock.StepIndex + step, 1);
		}
		Require(Nyx::HashTerrainEdits(GroundOf(loaded.GetRegistry())) == Nyx::HashTerrainEdits(GroundOf(ran)), "Digging on should make the same edits and bakes as without the load");

		// Undone after a load, back past what was saved, down to what the scene had: the ground as it was
		GameWorld undone;
		WorldSaveBase undoneBase;
		WorldSaveClock undoneClock;
		Require(undone.LoadSave(save, {}, undoneBase, undoneClock, error), "The save should load, but it " + error);
		Nyx::SdfTerrainComponent& undoing = undone.GetRegistry().Get<Nyx::SdfTerrainComponent>(Find(undone.GetRegistry(), GroundGuid));
		Require(undoing.Edits.size() > SceneEdits, "All the edits should be there to undo");
		undoing.Edits.resize(SceneEdits);
		Require(SampleGround(undoing) == asInTheScene, "Undoing after a load should give the ground as it was");

		// And undone into the scene's own, then dug anew, saved and loaded
		undoing.Edits.resize(SceneEdits - 2);
		Nyx::AppendTerrainEdit(undoing, Hole(70));
		const std::filesystem::path third = folder.Path / "Third.nyxsave";
		Require(undone.SaveToFile(undoneBase, undoneClock, third), "The world should be saved");
		GameWorld thirdWorld;
		WorldSaveBase thirdBase;
		WorldSaveClock thirdClock;
		Require(thirdWorld.LoadSave(third, {}, thirdBase, thirdClock, error), "The save should load, but it " + error);
		Require(Nyx::HashTerrainEdits(GroundOf(thirdWorld.GetRegistry())) == Nyx::HashTerrainEdits(undoing), "A scene's edit undone should stay undone");

		// A terrain made at run time: all its edits are the save's
		constexpr EntityGuid MadeGround{ 802 };
		{
			Registry& world = thirdWorld.GetRegistry();
			Nyx::SdfTerrainComponent terrain;
			Nyx::AppendTerrainEdit(terrain, Hole(1));
			Nyx::AppendTerrainBake(terrain, glm::ivec3(0));
			world.Add<Nyx::SdfTerrainComponent>(Add(world, MadeGround, "Made ground"), terrain);
			const std::filesystem::path fourth = folder.Path / "Fourth.nyxsave";
			Require(thirdWorld.SaveToFile(thirdBase, thirdClock, fourth), "The world should be saved");
			Registry back;
			WorldSaveBase backBase;
			WorldSaveClock backClock;
			MustLoad(fourth, back, backBase, backClock);
			const Nyx::SdfTerrainComponent& made = back.Get<Nyx::SdfTerrainComponent>(Find(back, MadeGround));
			Require(Nyx::HashTerrainEdits(made) == Nyx::HashTerrainEdits(terrain), "A terrain made at run time should have its edits");
			Require(CountBakes(made, true) == 1, "A terrain made at run time should have its bake's values");
		}

		// Cut off in its terrain's edits: refused
		{
			const std::vector<std::byte> bytes = ReadBytes(save);
			const std::filesystem::path cut = folder.Path / "Cut.nyxsave";
			WriteFile(cut, std::span<const std::byte>(bytes).first(bytes.size() - 10));
			RequireRefused(cut, "ends early", "A save cut off in its terrain's edits");
		}
	}

	// The ground under a save's edits has changed: it loads, its bakes making their values again
	void TestTerrainEditsOnOtherGround()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path save = folder.Path / "Dug.nyxsave";
		const std::filesystem::path scene = WriteGround(folder.Path);

		GameWorld running;
		WorldSaveBase base;
		StartWorld(running, scene, base);
		for (uint64_t step = 0; step < 10; ++step)
		{
			Dig(running, int(step), 2);
			RunSteps(running, step, 1);
		}
		const Nyx::SdfTerrainComponent dug = GroundOf(running.GetRegistry());
		const size_t saved = dug.Edits.size() - SceneEdits;
		const size_t bakes = CountBakes(dug, false);
		Require(bakes > 0, "Digging in one place should have baked it");
		Require(running.SaveToFile(base, WorldSaveClock{ 10, 10.0 * Step, Step }, save), "The world should be saved");
		const std::vector<std::byte> bytes = ReadBytes(save);

		// Another generator
		{
			std::vector<std::byte> other = bytes;
			const uint64_t fingerprint = Nyx::GetTerrainGeneratorFingerprint();
			const std::byte* first = reinterpret_cast<const std::byte*>(&fingerprint);
			const auto at = std::search(other.begin(), other.end(), first, first + sizeof(fingerprint));
			Require(at != other.end(), "A save should say which generator made its terrain");
			*at ^= std::byte{ 1 };
			const std::filesystem::path otherSave = folder.Path / "Other.nyxsave";
			WriteFile(otherSave, other);

			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			MustLoad(otherSave, world, loadedBase, clock);
			Require(Nyx::HashTerrainEdits(GroundOf(world)) == Nyx::HashTerrainEdits(dug), "Another generator should still give the same edits");
			Require(CountBakes(GroundOf(world), false) == bakes && CountBakes(GroundOf(world), true) == 0, "Another generator's bakes should make their values again");
			Require(SampleGround(GroundOf(world)) == SampleGround(dug), "With values made again, the same generator gives the same ground");
		}

		// Another seed in the scene, which the world didn't change: followed, the edits on top
		{
			WriteGround(folder.Path, 0, 2);
			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			MustLoad(save, world, loadedBase, clock);
			Require(GroundOf(world).Seed == 2, "A seed the world didn't change should follow the scene");
			Require(GroundOf(world).Edits.size() == dug.Edits.size(), "The edits should stay on other layers");
			Require(CountBakes(GroundOf(world), true) == 0, "Bakes of other layers should make their values again");
		}

		// New edits in the scene: first, and the save's on top, numbered on
		{
			WriteGround(folder.Path, 3);
			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			MustLoad(save, world, loadedBase, clock);
			const Nyx::SdfTerrainComponent& terrain = GroundOf(world);
			Require(terrain.Edits.size() == SceneEdits + 3 + saved, "The scene's new edits and the save's should all be there");
			for (size_t at = 0; at < terrain.Edits.size(); ++at)
			{
				Require(terrain.Edits[at].Revision == at + 1, "The revisions should count on");
			}
			Require(terrain.Edits[SceneEdits].Position == Hole(100 + int(SceneEdits)).Position, "The scene's new edit should come before the save's");
			Require(CountBakes(terrain, true) == 0, "Bakes on other edits should make their values again");
			Require(Nyx::RestoreTerrainBakes(world.Get<Nyx::SdfTerrainComponent>(Find(world, GroundGuid))) == bakes, "They should be made from what is there");
		}

		// The terrain gone from the scene: the rest loads
		{
			Registry empty;
			Add(empty, PlainGuid, "Plain");
			Require(Nyx::SceneSerializer::SaveToFile(empty, scene), "The scene should be saved");
			Registry world;
			WorldSaveBase loadedBase;
			WorldSaveClock clock;
			MustLoad(save, world, loadedBase, clock);
			Require(!Nyx::FindEntityByGuid(world, GroundGuid), "The save shouldn't make a terrain its scene lost");
		}
	}

	// Saves of versions before Terrains have a terrain's edits as a property of it
	void TestTerrainEditsAsAProperty()
	{
		const TestDirectory folder("NyxWorldSave");
		const std::filesystem::path scene = WriteGround(folder.Path);
		const std::filesystem::path save = folder.Path / "Old.nyxsave";

		Registry running;
		WorldSaveBase base;
		Require(Nyx::SceneSerializer::LoadFromFile(scene, running), "The scene should load");
		Require(Nyx::MakeWorldSaveBase(scene, running, base), "The world should give a base");

		Nyx::BinaryWriter header;
		Require(Nyx::WriteWorldSave(base, base.Entities, {}, header), "An unchanged world should be written");
		Nyx::BinaryWriter noRecords;
		noRecords.WriteUInt32(0);
		Nyx::BinaryWriter emptyTail;
		for (const char* name : { "Changed", "Made", "Instances", "Motions", "Terrains" })
		{
			emptyTail.WriteString(name);
			emptyTail.WriteBlock(noRecords);
		}

		Nyx::SdfTerrainComponent dug = GroundOf(running);
		Nyx::AppendTerrainEdit(dug, Hole(1));
		const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>();
		size_t editsProperty = type.PropertyCount;
		for (size_t index = 0; index < type.PropertyCount; ++index)
		{
			editsProperty = std::string_view(type.Properties[index].Name) == "Edits" ? index : editsProperty;
		}
		Nyx::BinaryWriter record;
		record.WriteUInt64(GroundGuid.Value);
		record.WriteUInt32(0);
		record.WriteUInt32(1);
		record.WriteString(type.Name);
		Require(Nyx::ReflectedArchiveSerializer::SerializeProperties(record, &dug, type, { editsProperty }), "The edits should be written");
		Nyx::BinaryWriter changed;
		changed.WriteUInt32(1);
		changed.WriteBlock(record);

		Nyx::BinaryWriter old;
		old.WriteBytes(header.GetBytes().data(), header.GetBytes().size() - emptyTail.GetBytes().size());
		old.WriteString("Changed");
		old.WriteBlock(changed);
		std::vector<std::byte> bytes = old.GetBytes();
		const uint32_t version = 2;
		std::memcpy(bytes.data() + sizeof(uint32_t), &version, sizeof(version));
		WriteFile(save, bytes);

		Registry world;
		WorldSaveBase loadedBase;
		WorldSaveClock clock;
		MustLoad(save, world, loadedBase, clock);
		Require(Nyx::HashTerrainEdits(GroundOf(world)) == Nyx::HashTerrainEdits(dug), "A terrain's edits as a property should load");
	}

	// Not a test: what taking a snapshot costs, the part of saving that holds up the world's thread
	void MeasureSnapshot()
	{
		for (const size_t count : { size_t{ 1000 }, size_t{ 5000 }, size_t{ 20000 } })
		{
			Registry world;
			for (size_t index = 0; index < count; ++index)
			{
				const Entity entity = Add(world, EntityGuid{ 1 + index }, "Entity");
				world.Add<Nyx::MeshRendererComponent>(entity);
				if (index % 2 == 0)
				{
					world.Add<Nyx::BoxColliderComponent>(entity);
					world.Add<Nyx::RigidBodyComponent>(entity);
				}
			}

			double snapshotMs = 1e9;
			double writeMs = 1e9;
			size_t bytes = 0;
			WorldSaveBase base;
			base.ScenePath = "Measured.nyxscene";
			Require(Nyx::TakeWorldSnapshot(world, base.Entities), "The world should give a snapshot");
			for (int run = 0; run < 5; ++run)
			{
				Nyx::WorldSnapshot snapshot;
				const auto start = std::chrono::steady_clock::now();
				Require(Nyx::TakeWorldSnapshot(world, snapshot), "The world should give a snapshot");
				const auto taken = std::chrono::steady_clock::now();
				Nyx::BinaryWriter out;
				Require(Nyx::WriteWorldSave(base, snapshot, {}, out), "The save should be written");
				const auto written = std::chrono::steady_clock::now();
				snapshotMs = std::min(snapshotMs, std::chrono::duration<double, std::milli>(taken - start).count());
				writeMs = std::min(writeMs, std::chrono::duration<double, std::milli>(written - taken).count());
				bytes = snapshot.Bytes.GetBytes().size();
			}
			std::cout << count << " entities: snapshot " << snapshotMs << " ms (" << bytes / 1024 << " KB), comparing " << writeMs << " ms\n";
		}

		// A terrain dug many times, a bake to each 20 edits: its edits copied on the world's thread,
		// written on a worker, its bakes' values there already
		for (const int count : { 1000, 10000, 100000 })
		{
			Registry world;
			Nyx::SdfTerrainComponent terrain;
			for (int n = 0; n < count; ++n)
			{
				Nyx::AppendTerrainEdit(terrain, Hole(n % 40));
				if (n % 20 == 0)
				{
					Nyx::AppendTerrainBake(terrain, glm::ivec3(n % 400 / 20, 0, 0));
				}
			}
			const Entity ground = Add(world, GroundGuid, "Ground");
			WorldSaveBase base;
			base.ScenePath = "Measured.nyxscene";
			Require(Nyx::TakeWorldSnapshot(world, base.Entities), "The world should give a snapshot");
			world.Add<Nyx::SdfTerrainComponent>(ground, std::move(terrain));
			Nyx::RestoreTerrainBakes(world.Get<Nyx::SdfTerrainComponent>(ground));

			double snapshotMs = 1e9;
			double writeMs = 1e9;
			size_t bytes = 0;
			for (int run = 0; run < 5; ++run)
			{
				Nyx::WorldSnapshot snapshot;
				const auto start = std::chrono::steady_clock::now();
				Require(Nyx::TakeWorldSnapshot(world, snapshot), "The world should give a snapshot");
				const auto taken = std::chrono::steady_clock::now();
				Nyx::BinaryWriter out;
				Require(Nyx::WriteWorldSave(base, snapshot, {}, out), "The save should be written");
				const auto written = std::chrono::steady_clock::now();
				snapshotMs = std::min(snapshotMs, std::chrono::duration<double, std::milli>(taken - start).count());
				writeMs = std::min(writeMs, std::chrono::duration<double, std::milli>(written - taken).count());
				bytes = out.GetBytes().size();
			}
			std::cout << count << " terrain edits: snapshot " << snapshotMs << " ms, writing " << writeMs << " ms (" << bytes / 1024 << " KB)\n";
		}
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		if (argc > 1 && std::string_view(argv[1]) == "--measure")
		{
			MeasureSnapshot();
			return 0;
		}
		TestARunningWorldComesBack();
		TestASaveIsTheDifferenceFromItsScene();
		TestChildrenStayWhereTheyWereLeft();
		TestTheClockGoesOn();
		TestASaveIsMadeFromItsSnapshot();
		TestOldNewAndDamagedSaves();
		TestInstancesMadeAtRunTimeComeBack();
		TestInstancesOfTheSceneComeBack();
		TestBodiesGoOnMoving();
		TestTerrainEditsComeBack();
		TestTerrainEditsOnOtherGround();
		TestTerrainEditsAsAProperty();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All world save tests passed\n";
	return 0;
}

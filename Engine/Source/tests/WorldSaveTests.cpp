// Save games: a world saved while it runs and loaded into a new one, as what differs from its scene,
// with entities made at run time, changed components and destroyed entities coming back by guid; a
// save of a loaded world still a difference from the scene; a scene changed under a save; children
// left behind by their parents staying there; the clock and the time of day going on; a save made on
// another thread from a snapshot; sections and types a build doesn't know, later versions, damage and
// a missing scene; instances of prefabs made at run time as what differs from their prefabs, following
// changes to them; those of the scene; bodies and characters going on as fast as they went. With
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
#include "SceneSerializer.h"
#include "SpawnPrefabCommand.h"
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All world save tests passed\n";
	return 0;
}

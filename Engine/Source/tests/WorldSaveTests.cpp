// Save games: a world saved while it runs and loaded into a new one, as what differs from its scene,
// with entities made at run time, changed components and destroyed entities coming back by guid; a
// save of a loaded world still a difference from the scene; a scene changed under a save; children
// left behind by their parents staying there; the clock and the time of day going on; a save made on
// another thread from a snapshot; sections and types a build doesn't know, later versions, damage and
// a missing scene. With --measure, in a Release build, what taking a snapshot costs.
#include "BinaryArchive.h"
#include "ComponentRegistration.h"
#include "EntityCommands.h"
#include "EntityGuidLookup.h"
#include "FixedStepClock.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "PointLightComponent.h"
#include "ReflectionUtils.h"
#include "SceneSerializer.h"
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

	// Whether two worlds hold the same saved values, entity by entity, whatever their order
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All world save tests passed\n";
	return 0;
}

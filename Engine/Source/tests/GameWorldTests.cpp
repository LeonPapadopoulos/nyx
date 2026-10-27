// The game world (GameWorld): its systems run each step in the order they were added, every step
// or every nth one, and see the world and the step they run in, and its entities by guid through
// a lookup made once a step without allocating; it keeps the air its game gives its particles.
#include "EntityGuidLookup.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "NameComponent.h"
#include "WindField.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

// Every allocation of this program counts, so a test can show that something makes none
namespace
{
	std::atomic<size_t> AllocationCount{ 0 };
}

void* operator new(size_t size)
{
	++AllocationCount;
	if (void* memory = std::malloc(size == 0 ? 1 : size))
	{
		return memory;
	}
	throw std::bad_alloc();
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, size_t /*size*/) noexcept
{
	std::free(memory);
}

namespace
{
	using Nyx::GameWorld;
	using Nyx::StepTime;
	using Nyx::Tests::Require;

	// Writes its name and the step's index into a shared log each time it runs
	class LoggingSystem final : public Nyx::ISimSystem
	{
	public:
		LoggingSystem(std::string name, std::vector<std::string>& log)
			: Name(std::move(name))
			, Log(log)
		{
		}

		const char* GetName() const override
		{
			return Name.c_str();
		}

		void Step(const Nyx::SimContext& context) override
		{
			Log.push_back(Name + std::to_string(context.Step.StepIndex));
		}

	private:
		std::string Name;
		std::vector<std::string>& Log;
	};

	StepTime MakeStep(uint64_t index)
	{
		return StepTime{ 1.0 / 60.0, index, static_cast<double>(index) / 60.0 };
	}

	void TestSystemsRunInTheirOrder()
	{
		std::vector<std::string> log;
		GameWorld world;
		world.AddSystem(std::make_unique<LoggingSystem>("A", log));
		world.AddSystem(std::make_unique<LoggingSystem>("B", log));
		world.AddSystem(std::make_unique<LoggingSystem>("C", log));

		world.Step(MakeStep(0));
		world.Step(MakeStep(1));

		Require(log == std::vector<std::string>{ "A0", "B0", "C0", "A1", "B1", "C1" },
			"Systems should run each step, in the order they were added");
		Require(world.GetSystems().size() == 3, "The world should list its three systems");
		Require(std::string(world.GetSystems()[1].System->GetName()) == "B", "The systems should be listed in their order");
	}

	void TestSystemsRunEveryNthStep()
	{
		std::vector<std::string> log;
		GameWorld world;
		world.AddSystem(std::make_unique<LoggingSystem>("Every", log));
		world.AddSystem(std::make_unique<LoggingSystem>("Third", log), 3);
		world.AddSystem(std::make_unique<LoggingSystem>("Zero", log), 0);

		for (uint64_t step = 0; step < 7; ++step)
		{
			world.Step(MakeStep(step));
		}

		const std::vector<std::string> expected{ "Every0", "Third0", "Zero0", "Every1", "Zero1", "Every2", "Zero2", "Every3",
			"Third3", "Zero3", "Every4", "Zero4", "Every5", "Zero5", "Every6", "Third6", "Zero6" };
		Require(log == expected, "A system should run every nth step from step 0; an interval of 0 means every step");
	}

	// Renames every entity, so the test can see a system change the world it runs in
	class RenamingSystem final : public Nyx::ISimSystem
	{
	public:
		const char* GetName() const override
		{
			return "Renaming";
		}

		void Step(const Nyx::SimContext& context) override
		{
			context.World.Each<Nyx::NameComponent>(
				[&](Nyx::Entity /*entity*/, Nyx::NameComponent& name)
				{
					name.Name = "Step " + std::to_string(context.Step.StepIndex);
				});
		}
	};

	void TestSystemsChangeTheirWorld()
	{
		GameWorld world;
		const Nyx::Entity entity = world.GetRegistry().CreateEntity();
		world.GetRegistry().Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ "Before" });
		world.AddSystem(std::make_unique<RenamingSystem>());

		world.Step(MakeStep(41));

		Require(world.GetRegistry().Get<Nyx::NameComponent>(entity).Name == "Step 41",
			"A system should change the world's entities, and see the step it runs in");
	}

	// Draws a line each step it runs, and a timed one in the first
	class DrawingSystem final : public Nyx::ISimSystem
	{
	public:
		const char* GetName() const override
		{
			return "Drawing";
		}

		void Step(const Nyx::SimContext& context) override
		{
			if (context.Step.StepIndex == 0)
			{
				context.Debug.Line(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.04f);
			}
			if (bDraw)
			{
				context.Debug.Line(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			}
		}

		bool bDraw = true;
	};

	void TestDebugDrawingLastsAStep()
	{
		if constexpr (!Nyx::DebugDraw::Enabled)
		{
			return;
		}

		GameWorld world;
		DrawingSystem& system = static_cast<DrawingSystem&>(world.AddSystem(std::make_unique<DrawingSystem>()));

		world.Step(MakeStep(0));
		Require(world.GetDebugDraw().GetLines().size() == 2, "A step's drawing should be there after the step");

		system.bDraw = false;
		world.Step(MakeStep(1));
		Require(world.GetDebugDraw().GetLines().size() == 1, "A step's drawing should go with the next step; the timed line stays");

		// 1/60 s steps: the 0.04 s line is gone after the third step's aging
		world.Step(MakeStep(2));
		world.Step(MakeStep(3));
		Require(world.GetDebugDraw().GetLines().empty(), "A timed line should go once its time in steps ran out");
	}

	using Nyx::Entity;
	using Nyx::EntityGuid;

	Entity CreateWithGuid(Nyx::Registry& world, EntityGuid guid)
	{
		const Entity entity = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ guid });
		return entity;
	}

	// Finds a guid through its step's lookup each time it runs
	class FindingSystem final : public Nyx::ISimSystem
	{
	public:
		explicit FindingSystem(EntityGuid guid)
			: Guid(guid)
		{
		}

		const char* GetName() const override
		{
			return "Finding";
		}

		void Step(const Nyx::SimContext& context) override
		{
			Found.push_back(Nyx::FindEntityByGuid(context.World, context.Guids, Guid));
		}

		EntityGuid Guid;
		std::vector<std::optional<Entity>> Found;
	};

	// Runs the test's function each step, to change the world in the middle of one
	class CallingSystem final : public Nyx::ISimSystem
	{
	public:
		explicit CallingSystem(std::function<void(const Nyx::SimContext&)> call)
			: Call(std::move(call))
		{
		}

		const char* GetName() const override
		{
			return "Calling";
		}

		void Step(const Nyx::SimContext& context) override
		{
			Call(context);
		}

	private:
		std::function<void(const Nyx::SimContext&)> Call;
	};

	template<typename T>
	T& AddSystem(GameWorld& world, std::unique_ptr<T> system, Nyx::ESimStage stage = Nyx::ESimStage::BeforePhysics)
	{
		return static_cast<T&>(world.AddSystem(std::move(system), 1, stage));
	}

	void TestSystemsFindEntitiesByGuid()
	{
		GameWorld world;
		const EntityGuid guid{ 0x10 };
		const Entity crate = CreateWithGuid(world.GetRegistry(), guid);
		FindingSystem& before = AddSystem(world, std::make_unique<FindingSystem>(guid));
		FindingSystem& after = AddSystem(world, std::make_unique<FindingSystem>(guid), Nyx::ESimStage::AfterPhysics);

		world.Step(MakeStep(0));
		Require(before.Found.back() == crate && after.Found.back() == crate,
			"Systems before and after the physics step should find an entity made before the step by its guid");

		world.GetRegistry().DestroyEntity(crate);
		world.Step(MakeStep(1));
		Require(!before.Found.back() && !after.Found.back(), "An entity destroyed between steps should be gone in the next step");
	}

	void TestTheLookupIsMadeOnceAStep()
	{
		GameWorld world;
		const EntityGuid guid{ 0x20 };
		std::optional<Entity> made;
		const auto makeThenDestroy = [&](const Nyx::SimContext& context)
		{
			if (context.Step.StepIndex == 0)
			{
				made = CreateWithGuid(context.World, guid);
			}
			else if (context.Step.StepIndex == 2)
			{
				context.World.DestroyEntity(*made);
			}
		};
		AddSystem(world, std::make_unique<CallingSystem>(makeThenDestroy));
		FindingSystem& finding = AddSystem(world, std::make_unique<FindingSystem>(guid));

		world.Step(MakeStep(0));
		Require(!finding.Found.back(), "An entity made during a step should not be in that step's lookup");
		world.Step(MakeStep(1));
		Require(finding.Found.back() == made, "An entity made during a step should be in the next step's lookup");
		world.Step(MakeStep(2));
		Require(!finding.Found.back(), "An entity destroyed during a step should be gone for the systems after");
	}

	void TestDuplicateGuidsResolveAsBefore()
	{
		GameWorld world;
		Nyx::Registry& registry = world.GetRegistry();
		const EntityGuid guid{ 0x30 };
		const Entity first = CreateWithGuid(registry, guid);
		const Entity second = CreateWithGuid(registry, guid);
		FindingSystem& finding = AddSystem(world, std::make_unique<FindingSystem>(guid));

		world.Step(MakeStep(0));
		Require(finding.Found.back() == first && Nyx::FindEntityByGuid(registry, guid) == first,
			"Of two entities with one guid, the lookup should find the first, as a search of the world does");

		registry.DestroyEntity(first);
		world.Step(MakeStep(1));
		Require(finding.Found.back() == second && Nyx::FindEntityByGuid(registry, guid) == second,
			"With the first gone, the lookup should find the second, as a search of the world does");
	}

	// Many guids, small and random, some repeated, some invalid, some entities destroyed: the lookup
	// finds what a search of the world finds, for each
	void TestTheLookupMatchesASearch()
	{
		Nyx::Registry world;
		std::vector<EntityGuid> guids;
		std::vector<Entity> entities;
		for (uint64_t i = 0; i < 1000; ++i)
		{
			EntityGuid guid = i % 2 == 0 ? EntityGuid{ i + 1 } : EntityGuid::Generate();
			if (i % 7 == 6)
			{
				guid = guids[i / 2];
			}
			guids.push_back(guid);
			entities.push_back(CreateWithGuid(world, i % 100 == 50 ? EntityGuid{} : guid));
		}
		for (size_t i = 0; i < entities.size(); i += 3)
		{
			world.DestroyEntity(entities[i]);
		}

		Nyx::EntityGuidLookup lookup;
		Require(!Nyx::FindEntityByGuid(world, lookup, guids[1]), "An empty lookup should find nothing");
		Nyx::RefreshEntityGuidLookup(world, lookup);

		guids.push_back(EntityGuid{});
		guids.push_back(EntityGuid{ 0xFFFF'FFFF'FFFF'FFFF });
		for (const EntityGuid guid : guids)
		{
			Require(Nyx::FindEntityByGuid(world, lookup, guid) == Nyx::FindEntityByGuid(world, guid),
				"The lookup should find what a search of the world finds, for guid " + Nyx::GuidToText(guid));
		}
	}

	void TestRefreshingAllocatesNothing()
	{
		Nyx::Registry world;
		std::vector<Entity> entities;
		for (int i = 0; i < 3000; ++i)
		{
			entities.push_back(CreateWithGuid(world, EntityGuid::Generate()));
		}

		// Counted before Require(), whose message is an allocation of its own
		Nyx::EntityGuidLookup lookup = Nyx::MakeEntityGuidLookup(world);
		size_t before = AllocationCount;
		Nyx::RefreshEntityGuidLookup(world, lookup);
		size_t allocations = AllocationCount - before;
		Require(allocations == 0, "Making the lookup again for an unchanged world should allocate nothing");

		const EntityGuid destroyed = world.Get<Nyx::GuidComponent>(entities[7]).Guid;
		const EntityGuid kept = world.Get<Nyx::GuidComponent>(entities[8]).Guid;
		world.DestroyEntity(entities[7]);
		before = AllocationCount;
		Nyx::RefreshEntityGuidLookup(world, lookup);
		allocations = AllocationCount - before;
		Require(allocations == 0, "Making the lookup again for a world with fewer guids should allocate nothing");
		Require(!Nyx::FindEntityByGuid(world, lookup, destroyed) && Nyx::FindEntityByGuid(world, lookup, kept) == entities[8],
			"The lookup made again should have the world's entities, and only those");
	}

	// The same air everywhere, as a game's would be somewhere
	class SteadyAir final : public Nyx::IWindField
	{
	public:
		void SampleWind(std::span<const glm::vec3> /*positions*/, float /*time*/, std::span<glm::vec3> outVelocities) const override
		{
			std::fill(outVelocities.begin(), outVelocities.end(), glm::vec3(1.0f, 0.0f, 0.0f));
		}

		void SampleWindGrid(const Nyx::WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities) const override
		{
			Nyx::SampleWindGridAtCentres(*this, grid, time, outVelocities);
		}
	};

	// A world has no air of its own until its game gives it one, which it keeps, untouched by its steps
	void TestTheWorldKeepsItsParticlesAir()
	{
		GameWorld world;
		Require(world.GetWindField() == nullptr, "A world should have no air of its game's until its game gives it one");
		const SteadyAir air;
		world.SetWindField(&air);
		world.Step(MakeStep(0));
		Require(world.GetWindField() == &air, "A world should keep the air its game gave it");
		world.SetWindField(nullptr);
		Require(world.GetWindField() == nullptr, "A world given no air should have none");
	}
}

int main()
{
	try
	{
		TestSystemsRunInTheirOrder();
		TestSystemsRunEveryNthStep();
		TestSystemsChangeTheirWorld();
		TestDebugDrawingLastsAStep();
		TestSystemsFindEntitiesByGuid();
		TestTheLookupIsMadeOnceAStep();
		TestDuplicateGuidsResolveAsBefore();
		TestTheLookupMatchesASearch();
		TestRefreshingAllocatesNothing();
		TestTheWorldKeepsItsParticlesAir();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game world test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game world tests passed.\n";
	return 0;
}

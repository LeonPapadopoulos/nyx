// The game world (GameWorld): its systems run each step in the order they were added, every step
// or every nth one, and see the world and the step they run in.
#include "GameWorld.h"
#include "NameComponent.h"
#include "TestSupport.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
}

int main()
{
	try
	{
		TestSystemsRunInTheirOrder();
		TestSystemsRunEveryNthStep();
		TestSystemsChangeTheirWorld();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game world test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game world tests passed.\n";
	return 0;
}

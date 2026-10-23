// A game module as a game builds one: NyxTestModule, made by nyx_add_game_module() from a folder
// outside the engine and linked with nyx_link_game_module(), as the game links its module. Its
// component types register next to the engine's, scenes save and load them, its own post-load step
// runs, and its systems run in a game world.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "Entity.h"
#include "GameModule.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "InputSystem.h"
#include "PlayerController.h"
#include "TestCommands.h"
#include "VirtualInputBackend.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestLanternComponent.h"
#include "TestMarkerComponent.h"
#include "TestSupport.h"

#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Nyx::ComponentTypeRegistry;
	using NyxTestModule::TestBrightenCommand;
	using NyxTestModule::TestLanternComponent;
	using NyxTestModule::TestMarkerComponent;
	using NyxTestModule::TestRelabelCommand;

	using Nyx::Tests::Require;

	constexpr std::string_view LanternTypeName = "NyxTestModule::TestLanternComponent";

	void TestTheLinkedModule()
	{
		const Nyx::GameModule& module = Nyx::GetLinkedGameModule();
		Require(std::string_view(module.Name) == "NyxTestModule", "The linked module should be the test module");
		Require(module.RegisterTypes != nullptr, "The module should register its types");
		Require(module.CreateSystems != nullptr, "The game's module should make its systems");
		Require(module.CreatePlayerController != nullptr, "The game's module should make its player");
		Require(module.RegisterSources == nullptr, "Only the editor's module registers source locations");
	}

	void TestModuleTypesRegisterNextToTheEngines()
	{
		// As the Application does: the engine's types, then the module's
		Nyx::RegisterComponentTypes();
		Nyx::GetLinkedGameModule().RegisterTypes();

		const ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		Require(registry.FindByName("Nyx::TransformComponent") != nullptr, "The engine's types should be registered");

		const Nyx::ComponentTypeOps* lantern = registry.FindByName(LanternTypeName);
		Require(lantern != nullptr, "The module's component type should be registered");
		Require(std::string_view(lantern->TypeMetadata->DisplayName) == "Test Lantern",
			"The module's type should have its reflected display name");
		Require(lantern->TypeMetadata->PropertyCount == 2, "The module's type should have its two properties");
	}

	void TestScenesKeepModuleComponents(const std::filesystem::path& directory)
	{
		Nyx::Registry world;
		const Nyx::Entity entity = world.CreateEntity();
		world.Add<TestLanternComponent>(entity, TestLanternComponent{ .Brightness = 2.5f, .Label = "Porch" });

		const std::filesystem::path scenePath = directory / "Module.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with a module component failed");

		Nyx::Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, loaded), "Loading a scene with a module component failed");

		int lanterns = 0;
		loaded.Each<TestLanternComponent>(
			[&](Nyx::Entity /*entity*/, TestLanternComponent& lantern)
			{
				++lanterns;
				Require(lantern.Brightness == 2.5f, "The module component's number was lost");
				Require(lantern.Label == "Porch", "The module component's text was lost");
			});
		Require(lanterns == 1, "The loaded scene should have the module component once");
	}

	void TestModuleComponentsKeepEngineStructs(const std::filesystem::path& directory)
	{
		// A module component holding the engine's SurfaceAnchor: the module's header tool run knows
		// the engine's types, and the anchor saves and loads field by field
		const Nyx::SurfaceAnchor anchor{ .Host = Nyx::EntityGuid{ 0x5EED },
			.LocalPosition = glm::vec3(0.5f, -1.0f, 2.0f),
			.LocalNormal = glm::vec3(0.0f, 0.0f, 1.0f),
			.LocalTangent = glm::vec3(0.0f, 1.0f, 0.0f) };

		Nyx::Registry world;
		world.Add<TestMarkerComponent>(world.CreateEntity(), TestMarkerComponent{ .Anchor = anchor });

		const std::filesystem::path scenePath = directory / "Marker.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with a surface anchor failed");

		Nyx::Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, loaded), "Loading a scene with a surface anchor failed");

		int markers = 0;
		loaded.Each<TestMarkerComponent>(
			[&](Nyx::Entity /*entity*/, TestMarkerComponent& marker)
			{
				++markers;
				Require(marker.Anchor.Host == anchor.Host, "The anchor's host was lost");
				Require(marker.Anchor.LocalPosition == anchor.LocalPosition, "The anchor's position was lost");
				Require(marker.Anchor.LocalNormal == anchor.LocalNormal, "The anchor's normal was lost");
				Require(marker.Anchor.LocalTangent == anchor.LocalTangent, "The anchor's tangent was lost");
			});
		Require(markers == 1, "The loaded scene should have the marker once");
	}

	void TestModulePostLoadStepRuns(const std::filesystem::path& directory)
	{
		// The module's REGISTER function set a post-load step: an unlabelled lantern is labelled
		Nyx::Registry world;
		world.Add<TestLanternComponent>(world.CreateEntity(), TestLanternComponent{ .Brightness = 1.0f });

		const std::filesystem::path scenePath = directory / "Unlabelled.nyxscene";
		Require(Nyx::SceneSerializer::SaveToFile(world, scenePath), "Saving the unlabelled lantern failed");

		Nyx::Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, loaded), "Loading the unlabelled lantern failed");
		loaded.Each<TestLanternComponent>(
			[](Nyx::Entity /*entity*/, TestLanternComponent& lantern)
			{
				Require(lantern.Label == "Lit", "The module's post-load step should have labelled the lantern");
			});
	}

	void TestModuleSystemsRunInTheGameWorld()
	{
		Nyx::GameWorld world;
		const Nyx::Entity entity = world.GetRegistry().CreateEntity();
		world.GetRegistry().Add<TestLanternComponent>(entity, TestLanternComponent{ .Brightness = 1.0f });

		// As the game's GameLayer does
		Nyx::GetLinkedGameModule().CreateSystems(world);
		Require(world.GetSystems().size() == 2, "The module should add its two systems");
		Require(std::string_view(world.GetSystems().back().System->GetName()) == "Lantern Glow",
			"The module's last system should be its Lantern Glow");

		for (uint64_t step = 0; step < 4; ++step)
		{
			world.Step(Nyx::StepTime{ 1.0 / 60.0, step, static_cast<double>(step) / 60.0 });
		}
		Require(world.GetRegistry().Get<TestLanternComponent>(entity).Brightness == 3.0f,
			"Four steps of the module's system should brighten the lantern by 2");
	}

	// A command keeps its Serialize properties only, in order, whatever its type
	void TestCommandsAreTheirProperties()
	{
		const Nyx::EntityGuid lantern = Nyx::EntityGuid::Generate();
		Nyx::CommandQueue queue;
		queue.Add(TestBrightenCommand{ .Lantern = lantern, .Amount = 1.5f, .LeftBehind = 9.0f });
		queue.Add(TestRelabelCommand{ .Label = "Porch" });
		queue.Add(TestBrightenCommand{ .Lantern = lantern, .Amount = 3.0f });
		Require(queue.GetCount() == 3, "The queue should hold three commands");

		std::vector<float> amounts;
		queue.ForEach<TestBrightenCommand>(
			[&](const TestBrightenCommand& command)
			{
				Require(command.Lantern == lantern, "A command should keep the guid it names");
				Require(command.LeftBehind == 0.0f, "What isn't a Serialize property shouldn't arrive");
				amounts.push_back(command.Amount);
			});
		Require(amounts == std::vector<float>{ 1.5f, 3.0f }, "Commands of a type should come in the order they were added");

		std::string label;
		queue.ForEach<TestRelabelCommand>([&](const TestRelabelCommand& command)
			{
				label = command.Label;
			});
		Require(label == "Porch", "Each type should get only its own commands");

		Nyx::CommandQueue next;
		next.Add(TestRelabelCommand{ .Label = "Gate" });
		queue.TakeFrom(next);
		std::vector<std::string> labels;
		queue.ForEach<TestRelabelCommand>([&](const TestRelabelCommand& command)
			{
				labels.push_back(command.Label);
			});
		Require(labels == std::vector<std::string>{ "Porch", "Gate" } && next.IsEmpty(), "Taken commands should follow, and leave the other empty");

		queue.Clear();
		Require(queue.IsEmpty(), "A cleared queue should be empty");
	}

	// The module's player, as the game's GameLayer drives it: input becomes commands, and only the
	// next step changes the world, once
	void TestThePlayersCommandsReachTheNextStep()
	{
		Nyx::GameWorld world;
		const Nyx::Entity entity = world.GetRegistry().CreateEntity();
		world.GetRegistry().Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ Nyx::EntityGuid::Generate() });
		world.GetRegistry().Add<TestLanternComponent>(entity, TestLanternComponent{ .Brightness = 1.0f });
		Nyx::GetLinkedGameModule().CreateSystems(world);

		Nyx::Input::VirtualInputBackend backend;
		Nyx::Input::InputSystem input;
		input.AddBackend(backend);
		const Nyx::Input::InputDeviceId keyboard = backend.AddDevice(Nyx::Input::EDeviceKind::Keyboard, "Keyboard", 1000);
		const std::unique_ptr<Nyx::IPlayerController> player = Nyx::GetLinkedGameModule().CreatePlayerController(input);
		Require(player != nullptr, "The module should make its player");

		uint64_t timeUs = 1000;
		uint64_t step = 0;
		const auto frame = [&](std::optional<Nyx::Ray> cursorRay)
		{
			timeUs += 16000;
			input.BeginFrame(timeUs);
			player->Update(Nyx::PlayerFrame{ .World = world, .Input = input, .Commands = world.GetCommands(), .CursorRay = cursorRay, .DeltaSeconds = 0.016f });
			input.EndFrame();
		};
		const auto stepOnce = [&]
		{
			world.Step(Nyx::StepTime{ 1.0 / 60.0, step, static_cast<double>(step) / 60.0 });
			++step;
		};
		const auto brightness = [&]
		{
			return world.GetRegistry().Get<TestLanternComponent>(entity).Brightness;
		};

		backend.Press(keyboard, Nyx::Input::Key(Nyx::Input::EKey::B), timeUs + 1000);
		frame(std::nullopt);
		Require(brightness() == 1.0f, "The player should only ask; the world changes in the step");
		Require(world.GetCommands().GetCount() == 1, "B should queue one command");

		stepOnce();
		Require(brightness() == 3.5f, "The step should apply the command (+2) before the glow (+0.5)");
		stepOnce();
		Require(brightness() == 4.0f, "A command should be applied by one step only");

		// Held down, B fires once; a frame without steps keeps its commands for the next step
		frame(std::nullopt);
		Require(world.GetCommands().IsEmpty(), "A held key shouldn't ask again");

		frame(Nyx::Ray{ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f) });
		frame(Nyx::Ray{ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f) });
		Require(world.GetCommands().GetCount() == 2, "Two frames without a step should keep both frames' commands");
		stepOnce();
		Require(world.GetRegistry().Get<TestLanternComponent>(entity).Label == "Pointed", "The cursor's ray should reach the player");
		Require(world.GetCommands().IsEmpty(), "The step should have taken them");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxGameModuleTests");
		TestTheLinkedModule();
		TestModuleTypesRegisterNextToTheEngines();
		TestScenesKeepModuleComponents(directory.Path);
		TestModuleComponentsKeepEngineStructs(directory.Path);
		TestModulePostLoadStepRuns(directory.Path);
		TestModuleSystemsRunInTheGameWorld();
		TestCommandsAreTheirProperties();
		TestThePlayersCommandsReachTheNextStep();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game module tests passed.\n";
	return 0;
}

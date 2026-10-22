// A game module as a game builds one: NyxTestModule, made by nyx_add_game_module() from a folder
// outside the engine and linked with nyx_link_game_module(), as the game links its module. Its
// component types register next to the engine's, scenes save and load them, its own post-load step
// runs, and its systems run in a game world.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "Entity.h"
#include "GameModule.h"
#include "GameWorld.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestLanternComponent.h"
#include "TestMarkerComponent.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
	using Nyx::ComponentTypeRegistry;
	using NyxTestModule::TestLanternComponent;
	using NyxTestModule::TestMarkerComponent;

	using Nyx::Tests::Require;

	constexpr std::string_view LanternTypeName = "NyxTestModule::TestLanternComponent";

	void TestTheLinkedModule()
	{
		const Nyx::GameModule& module = Nyx::GetLinkedGameModule();
		Require(std::string_view(module.Name) == "NyxTestModule", "The linked module should be the test module");
		Require(module.RegisterTypes != nullptr, "The module should register its types");
		Require(module.CreateSystems != nullptr, "The game's module should make its systems");
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
		Require(world.GetSystems().size() == 1, "The module should add its one system");
		Require(std::string_view(world.GetSystems().front().System->GetName()) == "Lantern Glow",
			"The module's system should be its Lantern Glow");

		for (uint64_t step = 0; step < 4; ++step)
		{
			world.Step(Nyx::StepTime{ 1.0 / 60.0, step, static_cast<double>(step) / 60.0 });
		}
		Require(world.GetRegistry().Get<TestLanternComponent>(entity).Brightness == 3.0f,
			"Four steps of the module's system should brighten the lantern by 2");
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game module tests passed.\n";
	return 0;
}

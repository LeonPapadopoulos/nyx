// A game module as a game builds one: NyxTestModule, made by nyx_add_game_module() from a folder
// outside the engine and linked with nyx_link_game_module(), as the game links its module. Its
// component types register next to the engine's, and scenes save and load them.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "Entity.h"
#include "GameModule.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestLanternComponent.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
	using Nyx::ComponentTypeRegistry;
	using NyxTestModule::TestLanternComponent;

	using Nyx::Tests::Require;

	constexpr std::string_view LanternTypeName = "NyxTestModule::TestLanternComponent";

	void TestTheLinkedModule()
	{
		const Nyx::GameModule& module = Nyx::GetLinkedGameModule();
		Require(std::string_view(module.Name) == "NyxTestModule", "The linked module should be the test module");
		Require(module.RegisterTypes != nullptr, "The module should register its types");
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "Game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All game module tests passed.\n";
	return 0;
}

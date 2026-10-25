// The component type registry: types register once, and a second type with a name already taken,
// such as a game module's type named like one of the engine's, is refused and the first one kept.
// Run-time types, reflected or not, are never saved, and are what InspectRuntimeComponents() gives.
// A type's provider of overridable values lists what an instance inherits.
#include "BinaryArchive.h"
#include "ComponentInspection.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "Log.h"
#include "NameComponent.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>

namespace
{
	using Nyx::ComponentTypeRegistry;
	using Nyx::Reflection::EReflectedTypeRole;
	using Nyx::Reflection::TypeMetadata;

	using Nyx::Tests::Require;

	// Component types made by hand, with the metadata the header tool would generate for them
	struct ModuleLanternComponent
	{
		float Brightness = 1.0f;
	};

	struct OtherModuleLanternComponent
	{
		float Brightness = 1.0f;
	};

	struct TakesEngineNameComponent
	{
		int Value = 0;
	};

	// Not reflected: what a game keeps of a system's own, e.g. behind a pointer
	struct HandleComponent
	{
		int Value = 0;
	};

	std::vector<Nyx::InspectedValue> DescribeHandle(const HandleComponent& handle)
	{
		return { { "Value", std::to_string(handle.Value) } };
	}

	std::vector<Nyx::InspectedValue> DescribeLantern(const ModuleLanternComponent& lantern)
	{
		return { { "Brightness", std::to_string(static_cast<int>(lantern.Brightness)) } };
	}
}

namespace Nyx::Reflection
{
	template<>
	const TypeMetadata& GetTypeMetadata<ModuleLanternComponent>()
	{
		static const TypeMetadata Metadata{ .Name = "Sample::LanternComponent", .Role = EReflectedTypeRole::Component };
		return Metadata;
	}

	template<>
	const TypeMetadata& GetTypeMetadata<OtherModuleLanternComponent>()
	{
		static const TypeMetadata Metadata{ .Name = "Sample::LanternComponent", .Role = EReflectedTypeRole::Component };
		return Metadata;
	}

	template<>
	const TypeMetadata& GetTypeMetadata<TakesEngineNameComponent>()
	{
		static const TypeMetadata Metadata{ .Name = "Nyx::NameComponent", .Role = EReflectedTypeRole::Component };
		return Metadata;
	}
}

namespace
{
	void TestTypesRegisterOnce()
	{
		ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		const size_t before = registry.GetAll().size();

		registry.Register<ModuleLanternComponent>();
		registry.Register<ModuleLanternComponent>();

		Require(registry.GetAll().size() == before + 1, "A type registered twice should be there once");
		const Nyx::ComponentTypeOps* lantern = registry.FindByName("Sample::LanternComponent");
		Require(lantern && lantern->TypeMetadata == &Nyx::Reflection::GetTypeMetadata<ModuleLanternComponent>(),
			"A registered type should be found by its name");
	}

	void TestSecondTypeOfANameIsRefused()
	{
		ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		const size_t before = registry.GetAll().size();

		registry.Register<OtherModuleLanternComponent>();

		Require(registry.GetAll().size() == before, "A second type with a name already taken should be refused");
		Require(!registry.FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<OtherModuleLanternComponent>()),
			"The refused type should not be registered");
		Require(registry.FindByName("Sample::LanternComponent")->TypeMetadata ==
				&Nyx::Reflection::GetTypeMetadata<ModuleLanternComponent>(),
			"The name should still find the type registered first");
	}

	void TestEngineTypesWinOverModuleTypes()
	{
		// The Application registers the engine's types before a game module's
		ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		Nyx::RegisterComponentTypes();

		registry.Register<TakesEngineNameComponent>();

		Require(registry.FindByName("Nyx::NameComponent")->TypeMetadata ==
				&Nyx::Reflection::GetTypeMetadata<Nyx::NameComponent>(),
			"A module type named like an engine type should leave the engine's in place");
		Require(!registry.FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<TakesEngineNameComponent>()),
			"A module type named like an engine type should be refused");
	}
}

namespace
{
	// A reflected type made a run-time type, and one that isn't reflected registered with metadata
	// of the registry's, once: neither is saved, data naming one is skipped, and both are inspected
	void TestRuntimeTypes()
	{
		ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		registry.SetRuntime<ModuleLanternComponent>();
		registry.SetDescribe<ModuleLanternComponent>(&DescribeLantern);
		Require(registry.FindByName("Sample::LanternComponent")->IsRuntime(), "A reflected type should be made a run-time type");

		const size_t before = registry.GetAll().size();
		registry.RegisterRuntime<HandleComponent>("Sample::HandleComponent", "Handle", &DescribeHandle);
		registry.RegisterRuntime<HandleComponent>("Sample::HandleComponent", "Handle", &DescribeHandle);
		Require(registry.GetAll().size() == before + 1, "A run-time type registered twice should be there once");
		const Nyx::ComponentTypeOps* handleType = registry.FindByName("Sample::HandleComponent");
		Require(handleType && handleType->IsRuntime() && std::string(handleType->TypeMetadata->DisplayName) == "Handle" &&
				handleType->TypeMetadata->PropertyCount == 0 && handleType->Describe,
			"A type that isn't reflected should get metadata of its own and its describe step");
		Require(!registry.FindByName("Nyx::NameComponent")->IsRuntime(), "Other types should stay saved");

		Nyx::Registry world;
		const Nyx::Entity entity = world.CreateEntity();
		world.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ "Lamp" });
		world.Add<ModuleLanternComponent>(entity, ModuleLanternComponent{ .Brightness = 3.0f });
		world.Add<HandleComponent>(entity, HandleComponent{ .Value = 12 });

		const std::vector<Nyx::InspectedComponent> inspected = Nyx::InspectRuntimeComponents(world, entity);
		Require(inspected.size() == 2 && inspected[0].Type == "Sample::LanternComponent" && inspected[1].Type == "Sample::HandleComponent",
			"Both run-time components should be inspected, in the registry's order, and the name not");
		Require(inspected[0].Values == std::vector<Nyx::InspectedValue>{ { "Brightness", "3" } } && inspected[1].Values == std::vector<Nyx::InspectedValue>{ { "Value", "12" } } &&
				inspected[1].DisplayName == "Handle" && inspected[1].Properties.empty(),
			"Each should say what it is");
		Require(Nyx::InspectRuntimeComponents(world, Nyx::Entity{}).empty(), "No entity should have nothing");

		// Saved without them
		Nyx::BinaryWriter writer;
		Require(Nyx::SceneSerializer::WriteEntity(world, entity, writer), "The entity should be written");
		Nyx::Registry loaded;
		const Nyx::Entity loadedEntity = loaded.CreateEntity();
		Nyx::BinaryReader reader;
		reader.LoadFromMemory(writer.GetBytes());
		Nyx::ScenePostLoadContext postLoad;
		Nyx::ReadWarnings warnings;
		Require(Nyx::SceneSerializer::ReadEntity(reader, loaded, loadedEntity, postLoad, warnings), "The entity should read");
		Require(loaded.Has<Nyx::NameComponent>(loadedEntity) && !loaded.Has<ModuleLanternComponent>(loadedEntity) && !loaded.Has<HandleComponent>(loadedEntity),
			"Run-time components should not be saved");

		// Data naming one, from a build where it was saved, is skipped and what follows still read
		Nyx::BinaryWriter old;
		old.WriteUInt32(2);
		old.WriteString("Sample::HandleComponent");
		Nyx::BinaryWriter emptyBlock;
		emptyBlock.WriteUInt16(0);
		old.WriteBlock(emptyBlock);
		old.WriteString("Nyx::NameComponent");
		Require(Nyx::ReflectedArchiveSerializer::SerializeObject(old, &world.Get<Nyx::NameComponent>(entity), Nyx::Reflection::GetTypeMetadata<Nyx::NameComponent>()),
			"The name should be written");
		const Nyx::Entity oldEntity = loaded.CreateEntity();
		reader.LoadFromMemory(old.GetBytes());
		Require(Nyx::SceneSerializer::ReadEntity(reader, loaded, oldEntity, postLoad, warnings), "Data naming a run-time type should still read");
		Require(!loaded.Has<HandleComponent>(oldEntity) && loaded.Has<Nyx::NameComponent>(oldEntity), "A run-time type in data should be skipped, and the rest read");
	}
}

namespace
{
	// The provider is asked with the component, which may decide what it lists; an override is the
	// first of its name, kept in its value's range when edited
	void TestOverridableValues()
	{
		ComponentTypeRegistry& registry = ComponentTypeRegistry::Get();
		const Nyx::ComponentTypeOps* lantern = registry.FindByName("Sample::LanternComponent");
		Require(lantern && !lantern->ListOverridableValues, "A type should have no provider at first");

		registry.SetOverridableValues<ModuleLanternComponent>(
			[](const ModuleLanternComponent& component, std::vector<Nyx::OverridableValue>& out)
			{
				out.push_back({ .Name = "Glow", .Inherited = component.Brightness * 2.0f, .Min = 0.0f, .Max = 10.0f, .Source = "Brightness" });
				if (component.Brightness > 2.0f)
				{
					out.push_back({ .Name = "Glare", .Inherited = 1.0f });
				}
			});
		Require(static_cast<bool>(lantern->ListOverridableValues), "The provider should be set");

		ModuleLanternComponent dim{ .Brightness = 1.0f };
		std::vector<Nyx::OverridableValue> values;
		lantern->ListOverridableValues(&dim, values);
		Require(values.size() == 1 && values[0].Name == "Glow" && values[0].Inherited == 2.0f && values[0].HasRange(),
			"A dim lantern should list its glow, inherited from its brightness");
		const ModuleLanternComponent bright{ .Brightness = 3.0f };
		values.clear();
		lantern->ListOverridableValues(&bright, values);
		Require(values.size() == 2 && values[1].Name == "Glare" && !values[1].HasRange(), "A bright one should list its glare too, without a range");

		Require(values[0].Clamp(12.0f) == 10.0f && values[0].Clamp(-1.0f) == 0.0f && values[0].Clamp(4.0f) == 4.0f, "A value should be kept in its range");
		Require(values[1].Clamp(1e30f) == 1e30f, "A value without a range should be kept as it is");

		const std::vector<Nyx::ValueOverride> overrides = { { "Glow", 5.0f }, { "Gone", 1.0f }, { "Glow", 7.0f } };
		Require(Nyx::FindValueOverride(overrides, "Glow") == &overrides[0] && !Nyx::FindValueOverride(overrides, "Glare"),
			"The first override of a name should be found, and none for a value not overridden");
		Require(Nyx::GetOverriddenValue(overrides, "Glow", 6.0f) == 5.0f && Nyx::GetOverriddenValue(overrides, "Glare", 1.0f) == 1.0f,
			"An instance should use its override, else what it inherits");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestTypesRegisterOnce();
		TestSecondTypeOfANameIsRefused();
		TestEngineTypesWinOverModuleTypes();
		TestRuntimeTypes();
		TestOverridableValues();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Component type registry test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All component type registry tests passed.\n";
	return 0;
}

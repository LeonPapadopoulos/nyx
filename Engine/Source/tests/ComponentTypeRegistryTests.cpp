// The component type registry: types register once, and a second type with a name already taken,
// such as a game module's type named like one of the engine's, is refused and the first one kept.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "Log.h"
#include "NameComponent.h"
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

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestTypesRegisterOnce();
		TestSecondTypeOfANameIsRefused();
		TestEngineTypesWinOverModuleTypes();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Component type registry test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All component type registry tests passed.\n";
	return 0;
}

#include "ComponentTypeRegistry.h"

#include <string>

namespace Nyx
{
	ComponentTypeRegistry& ComponentTypeRegistry::Get()
	{
		static ComponentTypeRegistry Instance;
		return Instance;
	}

	const ComponentTypeOps* ComponentTypeRegistry::FindByTypeMetadata(
		const Nyx::Reflection::TypeMetadata& typeMetadata) const
	{
		for (const ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == &typeMetadata)
			{
				return &ops;
			}
		}

		return nullptr;
	}

	const ComponentTypeOps* ComponentTypeRegistry::FindByName(std::string_view name) const
	{
		// Scene files and link recordings saved while the components were in Nyx::Engine name them
		// "Nyx::Engine::<Type>"; they are "Nyx::<Type>" now
		constexpr std::string_view OldNamespace = "Nyx::Engine::";
		if (name.starts_with(OldNamespace))
		{
			return FindByName("Nyx::" + std::string(name.substr(OldNamespace.size())));
		}

		for (const ComponentTypeOps& ops : Types)
		{
			if (std::string_view(ops.TypeMetadata->Name) == name)
			{
				return &ops;
			}
		}

		return nullptr;
	}
}

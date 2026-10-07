#include "ComponentTypeRegistry.h"

namespace Nyx::Engine
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

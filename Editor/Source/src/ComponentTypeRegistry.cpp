#include "ComponentTypeRegistry.h"

namespace Nyx::Editor
{
	ComponentTypeRegistry& ComponentTypeRegistry::Get()
	{
		static ComponentTypeRegistry Instance;
		return Instance;
	}

	void ComponentTypeRegistry::Register(ComponentTypeOps ops)
	{
		if (!ops.TypeMetadata)
		{
			return;
		}

		for (const ComponentTypeOps& existing : Types)
		{
			if (existing.TypeMetadata == ops.TypeMetadata)
			{
				return;
			}
		}

		Types.push_back(std::move(ops));
	}

	const ComponentTypeOps* ComponentTypeRegistry::FindByTypeMetadata(
		const Nyx::Reflection::TypeMetadata* typeMetadata) const
	{
		if (!typeMetadata)
		{
			return nullptr;
		}

		for (const ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == typeMetadata)
			{
				return &ops;
			}
		}

		return nullptr;
	}
}
#include "AssetTypeRegistry.h"

#include <algorithm>

namespace Nyx::Editor
{
	AssetTypeRegistry& AssetTypeRegistry::Get()
	{
		static AssetTypeRegistry Instance;
		return Instance;
	}

	void AssetTypeRegistry::Register(AssetTypeDescriptor descriptor)
	{
		auto it = std::find_if(
			Types.begin(),
			Types.end(),
			[&](const AssetTypeDescriptor& existing)
			{
				return existing.TypeId == descriptor.TypeId;
			});

		if (it != Types.end())
		{
			*it = std::move(descriptor);
			return;
		}

		Types.push_back(std::move(descriptor));
	}

	const AssetTypeDescriptor* AssetTypeRegistry::FindByTypeId(std::string_view typeId) const
	{
		auto it = std::find_if(
			Types.begin(),
			Types.end(),
			[&](const AssetTypeDescriptor& descriptor)
			{
				return descriptor.TypeId == typeId;
			});

		return (it != Types.end()) ? &(*it) : nullptr;
	}

	const AssetTypeDescriptor* AssetTypeRegistry::FindByExtension(std::string_view extension) const
	{
		for (const AssetTypeDescriptor& descriptor : Types)
		{
			for (const std::string& registeredExtension : descriptor.Extensions)
			{
				if (registeredExtension == extension)
				{
					return &descriptor;
				}
			}
		}

		return nullptr;
	}
}
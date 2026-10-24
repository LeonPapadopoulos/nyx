#include "NyxPCH.h"
#include "ComponentInspection.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "ReflectedArchiveSerializer.h"

namespace Nyx
{
	std::vector<InspectedComponent> InspectRuntimeComponents(const Registry& world, Entity entity)
	{
		std::vector<InspectedComponent> components;
		if (!world.IsAlive(entity))
		{
			return components;
		}

		for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
		{
			if (!ops.IsRuntime() || !ops.Has(world, entity))
			{
				continue;
			}

			const void* component = ops.GetConst(world, entity);
			InspectedComponent inspected;
			inspected.Type = ops.TypeMetadata->Name;
			inspected.DisplayName = ops.TypeMetadata->DisplayName;
			if (ops.TypeMetadata->PropertyCount > 0)
			{
				BinaryWriter writer;
				if (ReflectedArchiveSerializer::SerializeObject(writer, component, *ops.TypeMetadata))
				{
					inspected.Properties = writer.GetBytes();
				}
			}
			if (ops.Describe)
			{
				inspected.Values = ops.Describe(component);
			}
			components.push_back(std::move(inspected));
		}
		return components;
	}
}

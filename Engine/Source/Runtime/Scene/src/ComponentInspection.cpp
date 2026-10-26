#include "NyxPCH.h"
#include "ComponentInspection.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
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

	bool MarkInspectionRequest(Registry& world, EntityGuid entity, double now)
	{
		const std::optional<Entity> found = entity.IsValid() ? FindEntityByGuid(world, entity) : std::nullopt;
		if (!found)
		{
			return false;
		}
		if (world.Has<InspectionRequestComponent>(*found))
		{
			world.Get<InspectionRequestComponent>(*found).LastAsked = now;
		}
		else
		{
			world.Add<InspectionRequestComponent>(*found, InspectionRequestComponent{ .LastAsked = now });
		}
		return true;
	}

	void ForgetInspectionRequests(Registry& world, std::optional<double> now, double timeout)
	{
		std::vector<Entity> forgotten;
		world.Each<InspectionRequestComponent>(
			[&](Entity entity, InspectionRequestComponent& request)
			{
				if (!now || *now - request.LastAsked > timeout)
				{
					forgotten.push_back(entity);
				}
			});
		for (const Entity entity : forgotten)
		{
			world.Remove<InspectionRequestComponent>(entity);
		}
	}
}

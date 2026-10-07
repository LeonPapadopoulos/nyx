#include "SceneSerializer.h"

#include "ComponentTypeRegistry.h"
#include "Entity.h"
#include "ReflectedArchiveSerializer.h"

namespace Nyx::Engine
{
	bool SceneSerializer::SaveToFile(const Registry& world, const std::filesystem::path& path)
	{
		BinaryWriter writer;

		writer.WriteUInt32(SceneFileMagic);
		writer.WriteUInt32(SceneFileVersion);

		const auto& componentTypes = ComponentTypeRegistry::Get().GetAll();

		std::vector<Entity> entities;
		world.ForEachEntity([&](Entity entity)
			{
				entities.push_back(entity);
			});

		writer.WriteUInt32(static_cast<uint32_t>(entities.size()));

		for (Entity entity : entities)
		{
			const uint32_t entityId = static_cast<uint32_t>(entity.Value);
			writer.WriteUInt32(entityId);

			uint32_t componentCount = 0;
			for (const ComponentTypeOps& ops : componentTypes)
			{
				if (ops.Has(world, entity))
				{
					++componentCount;
				}
			}

			writer.WriteUInt32(componentCount);

			for (const ComponentTypeOps& ops : componentTypes)
			{
				if (!ops.Has(world, entity))
				{
					continue;
				}

				writer.WriteString(ops.TypeMetadata->Name);

				const void* component = ops.GetConst(world, entity);
				if (!ReflectedArchiveSerializer::SerializeObject(writer, component, *ops.TypeMetadata))
				{
					return false;
				}
			}
		}

		return writer.SaveToFile(path);
	}

	bool SceneSerializer::LoadFromFile(
		const std::filesystem::path& path,
		Registry& outWorld,
		ScenePostLoadContext postLoadContext)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			return false;
		}

		uint32_t magic = 0;
		uint32_t version = 0;

		if (!reader.ReadUInt32(magic) || magic != SceneFileMagic)
		{
			return false;
		}

		if (!reader.ReadUInt32(version) || version != SceneFileVersion)
		{
			return false;
		}

		outWorld.Clear();

		uint32_t entityCount = 0;
		if (!reader.ReadUInt32(entityCount))
		{
			return false;
		}

		for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			uint32_t serializedEntityId = 0;
			if (!reader.ReadUInt32(serializedEntityId))
			{
				return false;
			}

			Entity entity = outWorld.CreateEntity();

			uint32_t componentCount = 0;
			if (!reader.ReadUInt32(componentCount))
			{
				return false;
			}

			for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
			{
				std::string componentTypeName;
				if (!reader.ReadString(componentTypeName))
				{
					return false;
				}

				const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(componentTypeName);
				if (!ops)
				{
					return false;
				}

				void* component = ops->Add(outWorld, entity);

				if (!ReflectedArchiveSerializer::DeserializeObject(reader, component, *ops->TypeMetadata))
				{
					return false;
				}

				if (ops->PostLoad)
				{
					ops->PostLoad(component, postLoadContext);
				}
			}
		}

		return reader.IsValid();
	}
}
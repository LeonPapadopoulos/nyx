#include "SceneSerializer.h"

#include "ComponentTypeRegistry.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneDocument.h"

namespace Nyx::Editor
{
	bool SceneSerializer::SaveToFile(
		const Nyx::SceneDocument& scene,
		const std::filesystem::path& path)
	{
		using namespace Nyx::Engine;

		BinaryWriter writer;

		writer.WriteUInt32(SceneFileMagic);
		writer.WriteUInt32(SceneFileVersion);

		const Registry& registry = scene.GetRegistry();
		const auto& componentTypes = ComponentTypeRegistry::Get().GetAll();

		std::vector<Entity> entities;
		registry.ForEachEntity([&](Entity entity)
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
				if (ops.Has(registry, entity))
				{
					++componentCount;
				}
			}

			writer.WriteUInt32(componentCount);

			for (const ComponentTypeOps& ops : componentTypes)
			{
				if (!ops.Has(registry, entity))
				{
					continue;
				}

				writer.WriteString(ops.TypeMetadata->Name);

				const void* component = ops.GetConst(registry, entity);
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
		Nyx::SceneDocument& outScene,
		Nyx::Engine::ScenePostLoadContext postLoadContext)
	{
		using namespace Nyx::Engine;

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

		Registry& registry = outScene.GetRegistry();
		registry.Clear();

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

			Entity entity = registry.CreateEntity();

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

				void* component = ops->Add(registry, entity);

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
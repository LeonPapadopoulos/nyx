#include "SceneSerializer.h"

#include "ReflectedArchiveSerializer.h"
#include "SceneComponentRegistration.h"
#include "SceneComponentTypeRegistry.h"
#include "SceneDocument.h"

namespace Nyx::Editor
{
	bool SceneSerializer::SaveToFile(
		const Nyx::SceneDocument& scene,
		const std::filesystem::path& path)
	{
		using namespace Nyx::Engine;

		RegisterDefaultSceneComponentTypes();

		BinaryWriter writer;

		writer.WriteUInt32(SceneFileMagic);
		writer.WriteUInt32(SceneFileVersion);

		const Registry& registry = scene.GetRegistry();
		const auto& componentTypes = SceneComponentTypeRegistry::Get().GetAll();

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
			for (const SceneComponentTypeOps& ops : componentTypes)
			{
				if (ops.Has && ops.Has(registry, entity))
				{
					++componentCount;
				}
			}

			writer.WriteUInt32(componentCount);

			for (const SceneComponentTypeOps& ops : componentTypes)
			{
				if (!ops.Has || !ops.GetConst || !ops.TypeMetadata)
				{
					continue;
				}

				if (!ops.Has(registry, entity))
				{
					continue;
				}

				writer.WriteString(ops.SerializedTypeName);

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

		RegisterDefaultSceneComponentTypes();

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

				const SceneComponentTypeOps* ops =
					SceneComponentTypeRegistry::Get().FindBySerializedTypeName(componentTypeName);

				if (!ops || !ops->AddDefault || !ops->TypeMetadata)
				{
					return false;
				}

				void* component = ops->AddDefault(registry, entity);

				if (!ReflectedArchiveSerializer::DeserializeObject(reader, component, *ops->TypeMetadata))
				{
					return false;
				}

				if (ops->PostLoadResolve)
				{
					ops->PostLoadResolve(component, postLoadContext);
				}
			}
		}

		return reader.IsValid();
	}
}
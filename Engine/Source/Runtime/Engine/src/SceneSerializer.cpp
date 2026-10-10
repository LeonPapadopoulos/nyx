#include "SceneSerializer.h"

#include "ComponentTypeRegistry.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "Log.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	// Reads magic and version. On failure, outError says why.
	bool ReadHeader(BinaryReader& reader, uint32_t& outVersion, std::string& outError)
	{
		uint32_t magic = 0;
		if (!reader.ReadUInt32(magic) || magic != SceneFileMagic)
		{
			outError = "is not a scene file";
			return false;
		}

		if (!reader.ReadUInt32(outVersion))
		{
			outError = "ends early";
			return false;
		}

		if (outVersion < 1 || outVersion > SceneFileVersion)
		{
			outError = "is a scene file of version " + std::to_string(outVersion) + ", but this build reads versions 1 to " +
				std::to_string(SceneFileVersion);
			return false;
		}

		return true;
	}

	// Everything after the header: entity count, then the entities with their components
	bool WriteEntities(const Registry& world, BinaryWriter& writer)
	{
		std::vector<Entity> entities;
		world.ForEachEntity([&](Entity entity)
			{
				entities.push_back(entity);
			});

		writer.WriteUInt32(static_cast<uint32_t>(entities.size()));

		for (Entity entity : entities)
		{
			if (!SceneSerializer::WriteEntity(world, entity, writer))
			{
				return false;
			}
		}

		return true;
	}

	// Version 2: properties with name hash and kind. Component types this build doesn't know are skipped.
	bool ReadEntities(
		BinaryReader& reader,
		Registry& outWorld,
		ScenePostLoadContext& postLoadContext,
		ReadWarnings& warnings)
	{
		uint32_t entityCount = 0;
		if (!reader.ReadUInt32(entityCount))
		{
			return false;
		}

		for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			if (!SceneSerializer::ReadEntity(reader, outWorld, outWorld.CreateEntity(), postLoadContext, warnings))
			{
				return false;
			}
		}

		return true;
	}

	// Version 1: an unused number per entity, and property values without names or sizes,
	// so every component type has to be known and unchanged. On failure, outError says why.
	bool ReadEntitiesVersion1(
		BinaryReader& reader,
		Registry& outWorld,
		ScenePostLoadContext& postLoadContext,
		std::string& outError)
	{
		uint32_t entityCount = 0;
		if (!reader.ReadUInt32(entityCount))
		{
			return false;
		}

		for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			uint32_t unusedEntityNumber = 0;
			if (!reader.ReadUInt32(unusedEntityNumber))
			{
				return false;
			}

			const Entity entity = outWorld.CreateEntity();

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
					outError = "contains component type '" + componentTypeName +
						"', which this build doesn't know, and scene files of version 1 can't skip it";
					return false;
				}

				void* component = ops->Add(outWorld, entity);

				if (!ReflectedArchiveSerializer::DeserializeUntaggedObject(reader, component, *ops->TypeMetadata))
				{
					return false;
				}

				if (ops->PostLoad)
				{
					ops->PostLoad(component, postLoadContext);
				}
			}
		}

		return true;
	}

	// Every entity of a scene needs a guid that no other entity in it has. Entities from older files
	// get one, and when two entities have the same guid (e.g. in a file edited or merged by hand),
	// the second one gets a new guid.
	void GiveEveryEntityAUniqueGuid(Registry& world, ReadWarnings& warnings)
	{
		std::unordered_set<EntityGuid> usedGuids;
		std::vector<Entity> entitiesNeedingGuid;

		// Each guid stays with the first entity that has it
		world.ForEachEntity([&](Entity entity)
			{
				if (!world.Has<GuidComponent>(entity) || !world.Get<GuidComponent>(entity).Guid.IsValid())
				{
					warnings.Add("gave an entity without a guid a new one; saving the scene keeps it");
					entitiesNeedingGuid.push_back(entity);
				}
				else if (!usedGuids.insert(world.Get<GuidComponent>(entity).Guid).second)
				{
					warnings.Add("gave an entity a new guid, because another entity in the scene already had " +
						ReflectedArchivePrinter::ValueToText(world.Get<GuidComponent>(entity).Guid.Value));
					entitiesNeedingGuid.push_back(entity);
				}
			});

		for (Entity entity : entitiesNeedingGuid)
		{
			EntityGuid guid = EntityGuid::Generate();
			while (!usedGuids.insert(guid).second)
			{
				guid = EntityGuid::Generate();
			}

			if (!world.Has<GuidComponent>(entity))
			{
				world.Add<GuidComponent>(entity);
			}

			world.Get<GuidComponent>(entity).Guid = guid;
		}
	}

	// Prints what WriteEntities wrote
	bool PrintEntities(BinaryReader& reader, std::string& outText)
	{
		uint32_t entityCount = 0;
		if (!reader.ReadUInt32(entityCount))
		{
			return false;
		}

		outText += std::to_string(entityCount) + " entities\n";

		for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			outText += "\nEntity " + std::to_string(entityIndex) + "\n";

			if (!SceneSerializer::PrintEntity(reader, 1, outText))
			{
				return false;
			}
		}

		return true;
	}
}

namespace Nyx::Engine
{
	bool SceneSerializer::SaveToFile(const Registry& world, const std::filesystem::path& path)
	{
		BinaryWriter writer;

		writer.WriteUInt32(SceneFileMagic);
		writer.WriteUInt32(SceneFileVersion);

		if (!WriteEntities(world, writer))
		{
			return false;
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

		uint32_t version = 0;
		std::string error;

		if (!ReadHeader(reader, version, error))
		{
			LOG_ERROR("'{0}' {1}", path.string(), error);
			return false;
		}

		// Read into a separate world, so that outWorld stays as it was if the file is damaged
		Registry loadedWorld;

		error = "ends early or is damaged";
		ReadWarnings warnings;

		const bool bRead = (version == 1)
			? ReadEntitiesVersion1(reader, loadedWorld, postLoadContext, error)
			: ReadEntities(reader, loadedWorld, postLoadContext, warnings);

		if (!bRead || !reader.IsValid())
		{
			warnings.Log(path.string());
			LOG_ERROR("'{0}' {1}", path.string(), error);
			return false;
		}

		GiveEveryEntityAUniqueGuid(loadedWorld, warnings);
		warnings.Log(path.string());

		// Moving keeps outWorld at its address, so pointers to it (such as the renderer's) stay valid
		outWorld = std::move(loadedWorld);

		if (version < SceneFileVersion)
		{
			LOG_INFO("'{0}' is a scene file of version {1}; saving it converts it to version {2}",
				path.string(), version, SceneFileVersion);
		}

		return true;
	}

	bool SceneSerializer::WriteEntity(const Registry& world, Entity entity, BinaryWriter& writer)
	{
		const auto& componentTypes = ComponentTypeRegistry::Get().GetAll();

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

		return true;
	}

	bool SceneSerializer::ReadEntity(
		BinaryReader& reader,
		Registry& world,
		Entity entity,
		ScenePostLoadContext& postLoadContext,
		ReadWarnings& warnings)
	{
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
				warnings.Add("skipped component type '" + componentTypeName +
					"', which this build doesn't know; saving the scene drops it");

				if (!ReflectedArchiveSerializer::SkipObject(reader))
				{
					return false;
				}

				continue;
			}

			void* component = ops->Add(world, entity);

			if (!ReflectedArchiveSerializer::DeserializeObject(reader, component, *ops->TypeMetadata, warnings))
			{
				return false;
			}

			if (ops->PostLoad)
			{
				ops->PostLoad(component, postLoadContext);
			}
		}

		return true;
	}

	bool SceneSerializer::PrintEntity(BinaryReader& reader, int indentLevel, std::string& outText)
	{
		uint32_t componentCount = 0;
		if (!reader.ReadUInt32(componentCount))
		{
			return false;
		}

		const std::string indent(static_cast<size_t>(indentLevel) * 2, ' ');

		for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
		{
			std::string componentTypeName;
			if (!reader.ReadString(componentTypeName))
			{
				return false;
			}

			// Unknown component types are printed too, with name hashes instead of property names
			const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(componentTypeName);
			outText += indent + componentTypeName + (ops ? "" : " (a component type this build doesn't know)") + "\n";

			if (!ReflectedArchivePrinter::PrintObject(reader, ops ? ops->TypeMetadata : nullptr, indentLevel + 1, outText))
			{
				return false;
			}
		}

		return true;
	}

	bool SceneSerializer::PrintFile(const std::filesystem::path& path, std::string& outText)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			outText += "Can't open '" + path.string() + "'.\n";
			return false;
		}

		uint32_t version = 0;
		std::string error;

		if (!ReadHeader(reader, version, error))
		{
			outText += "'" + path.string() + "' " + error + ".\n";
			return false;
		}

		outText += "Scene file '" + path.string() + "'\n";

		if (version == SceneFileVersion)
		{
			outText += "Version " + std::to_string(version) + "\n";
		}
		else
		{
			// Older versions store values without names, so they are printed the way saving
			// would write them: loaded like in the editor, then written to memory.
			Registry world;
			BinaryWriter converted;

			if (!LoadFromFile(path, world) || !WriteEntities(world, converted))
			{
				outText += "Version " + std::to_string(version) + ", which can't be loaded; see the log.\n";
				return false;
			}

			reader.LoadFromMemory(converted.GetBytes());

			outText += "Version " + std::to_string(version) + ", shown as converted to version " +
				std::to_string(SceneFileVersion) + ". Saving the scene in the editor converts the file.\n";
		}

		if (!PrintEntities(reader, outText))
		{
			outText += "The file ends early or is damaged here.\n";
			return false;
		}

		return true;
	}
}
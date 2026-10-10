#include "NyxPCH.h"
#include "LiveEdits.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "Log.h"
#include "ReflectedArchivePrinter.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"

#include <string>

namespace
{
	using namespace Nyx::Engine;

	EntityGuid GetGuid(const Registry& world, Entity entity)
	{
		return world.Has<GuidComponent>(entity) ? world.Get<GuidComponent>(entity).Guid : EntityGuid{};
	}

	std::string GuidToText(EntityGuid guid)
	{
		return ReflectedArchivePrinter::ValueToText(guid.Value);
	}

	ELiveEditResult ApplySetProperties(const SetPropertiesMessage& message, Registry& world, ScenePostLoadContext& postLoadContext)
	{
		const std::optional<Entity> entity = FindEntityByGuid(world, message.Entity);
		if (!entity)
		{
			LOG_WARNING("Live edit: ignored new values for {0} of entity {1}, which isn't in this world", message.ComponentType,
				GuidToText(message.Entity));
			return ELiveEditResult::Ignored;
		}

		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(message.ComponentType);
		if (!ops)
		{
			LOG_WARNING("Live edit: ignored new values for {0} of entity {1}, a component type this build doesn't know",
				message.ComponentType, GuidToText(message.Entity));
			return ELiveEditResult::Ignored;
		}

		// Only the properties that changed are sent, so a component made here would have default
		// values for the others. A new component comes with the whole entity (CreateEntity).
		void* component = ops->Get(world, *entity);
		if (!component)
		{
			LOG_WARNING("Live edit: ignored new values for {0} of entity {1}, which doesn't have that component in this world",
				message.ComponentType, GuidToText(message.Entity));
			return ELiveEditResult::Ignored;
		}

		BinaryReader reader;
		reader.LoadFromMemory(message.Properties);
		ReadWarnings warnings;
		const bool bRead = ReflectedArchiveSerializer::DeserializeObject(reader, component, *ops->TypeMetadata, warnings);
		warnings.Log("Live edit of entity " + GuidToText(message.Entity));

		// Also after a damaged message: the component keeps what was read, and its assets have to match
		if (ops->PostLoad)
		{
			ops->PostLoad(component, postLoadContext);
		}

		if (!bRead)
		{
			LOG_ERROR("Live edit: the new values for {0} of entity {1} are cut off or damaged", message.ComponentType,
				GuidToText(message.Entity));
			return ELiveEditResult::Unreadable;
		}

		return ELiveEditResult::Applied;
	}

	ELiveEditResult ApplyCreateEntity(const CreateEntityMessage& message, Registry& world, ScenePostLoadContext& postLoadContext)
	{
		BinaryReader reader;
		reader.LoadFromMemory(message.Entity);
		ReadWarnings warnings;

		const Entity entity = world.CreateEntity();
		const bool bRead = SceneSerializer::ReadEntity(reader, world, entity, postLoadContext, warnings) && reader.IsValid();
		const EntityGuid guid = GetGuid(world, entity);
		warnings.Log("Live edit of entity " + GuidToText(guid));

		if (!bRead)
		{
			world.DestroyEntity(entity);
			LOG_ERROR("Live edit: a new entity ({0}) is cut off or damaged", GuidToText(guid));
			return ELiveEditResult::Unreadable;
		}

		if (!guid.IsValid())
		{
			world.DestroyEntity(entity);
			LOG_WARNING("Live edit: ignored a new entity without a guid");
			return ELiveEditResult::Ignored;
		}

		// The entity it replaces. Usually there is none, since the editor sends the entities that
		// edits and undo create, but the world should never have two entities with one guid.
		std::vector<Entity> replaced;
		world.Each<GuidComponent>(
			[&](Entity other, const GuidComponent& otherGuid)
			{
				if (other != entity && otherGuid.Guid == guid)
				{
					replaced.push_back(other);
				}
			});

		for (const Entity other : replaced)
		{
			world.DestroyEntity(other);
		}

		return ELiveEditResult::Applied;
	}

	ELiveEditResult ApplyDeleteEntity(const DeleteEntityMessage& message, Registry& world)
	{
		const std::optional<Entity> entity = FindEntityByGuid(world, message.Entity);
		if (!entity)
		{
			LOG_WARNING("Live edit: ignored deleting entity {0}, which isn't in this world", GuidToText(message.Entity));
			return ELiveEditResult::Ignored;
		}

		world.DestroyEntity(*entity);
		return ELiveEditResult::Applied;
	}

	template <typename TMessage, typename TApply>
	ELiveEditResult ReadAndApply(const Nyx::Net::Message& message, const char* name, TApply apply)
	{
		TMessage typedMessage;
		if (!ReadNetMessage(message, typedMessage))
		{
			LOG_ERROR("Live edit: a {0} message can't be read ({1} bytes)", name, message.Payload.size());
			return ELiveEditResult::Unreadable;
		}

		return apply(typedMessage);
	}
}

namespace Nyx::Engine
{
	std::optional<SetPropertiesMessage> MakeSetPropertiesMessage(
		const Registry& world,
		Entity entity,
		const ComponentTypeOps& componentType,
		const std::vector<size_t>& propertyIndices)
	{
		const EntityGuid guid = GetGuid(world, entity);
		const void* component = componentType.GetConst(world, entity);
		if (!guid.IsValid() || !component)
		{
			return std::nullopt;
		}

		const Nyx::Reflection::TypeMetadata& typeMetadata = *componentType.TypeMetadata;

		bool bAnySaved = false;
		for (const size_t propertyIndex : propertyIndices)
		{
			bAnySaved = bAnySaved || (propertyIndex < typeMetadata.PropertyCount &&
				Nyx::Reflection::HasFlag(typeMetadata.Properties[propertyIndex].Flags, Nyx::Reflection::EPropertyFlags::Serialize));
		}

		BinaryWriter properties;
		if (!bAnySaved || !ReflectedArchiveSerializer::SerializeProperties(properties, component, typeMetadata, propertyIndices))
		{
			return std::nullopt;
		}

		SetPropertiesMessage message;
		message.Entity = guid;
		message.ComponentType = typeMetadata.Name;
		message.Properties = properties.GetBytes();
		return message;
	}

	std::optional<CreateEntityMessage> MakeCreateEntityMessage(const Registry& world, Entity entity)
	{
		BinaryWriter writer;
		if (!GetGuid(world, entity).IsValid() || !SceneSerializer::WriteEntity(world, entity, writer))
		{
			return std::nullopt;
		}

		CreateEntityMessage message;
		message.Entity = writer.GetBytes();
		return message;
	}

	ELiveEditResult ApplyLiveEdit(const Net::Message& message, Registry& world, ScenePostLoadContext postLoadContext)
	{
		switch (static_cast<EEditorLinkMessage>(message.Type))
		{
		case EEditorLinkMessage::SetProperties:
			return ReadAndApply<SetPropertiesMessage>(message, "SetProperties",
				[&](const SetPropertiesMessage& setProperties)
				{
					return ApplySetProperties(setProperties, world, postLoadContext);
				});

		case EEditorLinkMessage::CreateEntity:
			return ReadAndApply<CreateEntityMessage>(message, "CreateEntity",
				[&](const CreateEntityMessage& createEntity)
				{
					return ApplyCreateEntity(createEntity, world, postLoadContext);
				});

		case EEditorLinkMessage::DeleteEntity:
			return ReadAndApply<DeleteEntityMessage>(message, "DeleteEntity",
				[&](const DeleteEntityMessage& deleteEntity)
				{
					return ApplyDeleteEntity(deleteEntity, world);
				});

		default:
			return ELiveEditResult::NotALiveEdit;
		}
	}
}

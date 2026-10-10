#include "GameLinkSubscriber.h"

#include "ComponentTypeRegistry.h"
#include "EditorLinkMessages.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "ReflectionUtils.h"
#include "SceneDocument.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
	using namespace Nyx::Editor;
	using Nyx::Engine::ComponentTypeOps;
	using Nyx::Engine::Entity;
	using Nyx::Engine::EntityGuid;
	using Nyx::Reflection::TypeMetadata;

	// Changed properties (indices) of one type
	template <typename TType>
	using PropertiesByType = std::vector<std::pair<const TType*, std::vector<size_t>>>;

	template <typename TType>
	void AddProperties(PropertiesByType<TType>& propertiesByType, const TType* type, const std::vector<size_t>& propertyIndices)
	{
		auto it = std::find_if(propertiesByType.begin(), propertiesByType.end(),
			[type](const auto& entry)
			{
				return entry.first == type;
			});

		if (it == propertiesByType.end())
		{
			it = propertiesByType.insert(propertiesByType.end(), { type, {} });
		}

		it->second.insert(it->second.end(), propertyIndices.begin(), propertyIndices.end());
	}

	// What one transaction did to one entity
	struct EntityEdit
	{
		Entity Handle{};

		// Added, deleted, or brought back by undo or redo
		bool bAddedOrDeleted = false;

		// From the snapshot of an added or deleted entity, for when it is gone afterwards
		EntityGuid Guid;

		// By the type they belong to: a component type, or a struct inside components (such as an
		// AssetReference), which is how the details panel records edits of a struct's fields
		PropertiesByType<TypeMetadata> ChangedProperties;
	};

	EntityEdit& FindOrAddEdit(std::vector<EntityEdit>& edits, const ObjectRef& target)
	{
		Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(target.Id.Value);

		for (EntityEdit& edit : edits)
		{
			if (edit.Handle == entity)
			{
				return edit;
			}
		}

		EntityEdit& edit = edits.emplace_back();
		edit.Handle = entity;
		return edit;
	}

	// The guid in a snapshot of an entity
	EntityGuid FindGuid(const RootObjectSnapshot& snapshot)
	{
		const TypeMetadata& guidType = Nyx::Reflection::GetTypeMetadata<Nyx::Engine::GuidComponent>();

		for (const ReflectedObjectSnapshot& subobject : snapshot.Subobjects)
		{
			if (subobject.TypeMetadata != &guidType)
			{
				continue;
			}

			for (const ReflectedPropertySnapshot& property : subobject.Properties)
			{
				const uint64_t* value = std::get_if<uint64_t>(&property.Value);
				if (value && property.PropertyIndex < guidType.PropertyCount &&
					std::string_view(guidType.Properties[property.PropertyIndex].Name) == "Guid")
				{
					return EntityGuid{ *value };
				}
			}
		}

		return {};
	}

	// Whether the type is the struct type, or has it inside, also nested deeper
	bool ContainsStruct(const TypeMetadata& type, const TypeMetadata& structType, int depth = 0)
	{
		if (&type == &structType)
		{
			return true;
		}

		// Real types nest a few levels at most; this keeps a type that contains itself from recursing forever
		constexpr int MaxDepth = 16;
		if (depth >= MaxDepth)
		{
			return false;
		}

		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const TypeMetadata* nestedType = Nyx::Reflection::TryGetNestedType(type.Properties[i]);
			if (nestedType && ContainsStruct(*nestedType, structType, depth + 1))
			{
				return true;
			}
		}

		return false;
	}

	// The entity's changed properties by component. An edit inside a struct is sent as the whole
	// struct property of the component; which of several such properties it was isn't recorded,
	// so each that contains the struct type is sent.
	PropertiesByType<ComponentTypeOps> GetChangedComponentProperties(const Nyx::Engine::Registry& world, const EntityEdit& edit)
	{
		const Nyx::Engine::ComponentTypeRegistry& componentTypes = Nyx::Engine::ComponentTypeRegistry::Get();
		PropertiesByType<ComponentTypeOps> changed;

		for (const auto& [type, propertyIndices] : edit.ChangedProperties)
		{
			if (const ComponentTypeOps* ops = componentTypes.FindByTypeMetadata(*type))
			{
				AddProperties(changed, ops, propertyIndices);
				continue;
			}

			for (const ComponentTypeOps& ops : componentTypes.GetAll())
			{
				if (!ops.Has(world, edit.Handle))
				{
					continue;
				}

				std::vector<size_t> structProperties;
				for (size_t i = 0; i < ops.TypeMetadata->PropertyCount; ++i)
				{
					const TypeMetadata* nestedType = Nyx::Reflection::TryGetNestedType(ops.TypeMetadata->Properties[i]);
					if (nestedType && ContainsStruct(*nestedType, *type))
					{
						structProperties.push_back(i);
					}
				}

				if (!structProperties.empty())
				{
					AddProperties(changed, &ops, structProperties);
				}
			}
		}

		return changed;
	}
}

namespace Nyx::Editor
{
	GameLinkSubscriber::GameLinkSubscriber(const Nyx::SceneDocument& scene)
		: Scene(scene)
	{
	}

	void GameLinkSubscriber::OnTransactionCommitted(const Transaction& transaction)
	{
		AddMessagesFor(transaction);
	}

	void GameLinkSubscriber::OnTransactionApplied(const Transaction& transaction, bool /*bWasUndo*/)
	{
		// Undo and redo are sent the same way as new edits: as the state the editor has afterwards
		AddMessagesFor(transaction);
	}

	void GameLinkSubscriber::StartSession()
	{
		Pending.clear();
		PendingBytes = 0;
		bSessionActive = true;
	}

	void GameLinkSubscriber::EndSession()
	{
		Pending.clear();
		PendingBytes = 0;
		bSessionActive = false;
	}

	std::vector<Nyx::Net::Message> GameLinkSubscriber::TakeMessages()
	{
		PendingBytes = 0;
		return std::exchange(Pending, {});
	}

	void GameLinkSubscriber::Keep(Nyx::Net::Message message)
	{
		// The session may have ended while one transaction's messages were made
		if (!bSessionActive)
		{
			return;
		}

		PendingBytes += message.Payload.size();
		Pending.push_back(std::move(message));

		if (PendingBytes > MaxPendingBytes)
		{
			LOG_WARNING("Game link: {0} MB of edits are waiting for a game that doesn't take them; it won't get edits until the next Play",
				PendingBytes / (1024 * 1024));
			EndSession();
		}
	}

	void GameLinkSubscriber::AddMessagesFor(const Transaction& transaction)
	{
		if (!bSessionActive)
		{
			return;
		}

		// Which entities the transaction touched, in the order of their first change
		std::vector<EntityEdit> edits;

		for (const Change& change : transaction.Changes)
		{
			if (const SetValueChange* setValue = std::get_if<SetValueChange>(&change.Payload))
			{
				if (setValue->Target.Domain == EObjectDomain::SceneEntity && setValue->TypeMetadata)
				{
					AddProperties(FindOrAddEdit(edits, setValue->Target).ChangedProperties, setValue->TypeMetadata,
						{ setValue->PropertyIndex });
				}
			}
			else if (const AddObjectChange* addObject = std::get_if<AddObjectChange>(&change.Payload))
			{
				if (addObject->Target.Domain == EObjectDomain::SceneEntity)
				{
					EntityEdit& edit = FindOrAddEdit(edits, addObject->Target);
					edit.bAddedOrDeleted = true;
					edit.Guid = edit.Guid.IsValid() ? edit.Guid : FindGuid(addObject->AfterCreate);
				}
			}
			else if (const DeleteObjectChange* deleteObject = std::get_if<DeleteObjectChange>(&change.Payload))
			{
				if (deleteObject->Target.Domain == EObjectDomain::SceneEntity)
				{
					EntityEdit& edit = FindOrAddEdit(edits, deleteObject->Target);
					edit.bAddedOrDeleted = true;
					edit.Guid = edit.Guid.IsValid() ? edit.Guid : FindGuid(deleteObject->BeforeDelete);
				}
			}
		}

		const Nyx::Engine::Registry& world = Scene.GetRegistry();

		for (const EntityEdit& edit : edits)
		{
			const bool bAlive = world.IsAlive(edit.Handle);

			if (bAlive && !world.Has<Nyx::Engine::GuidComponent>(edit.Handle))
			{
				LOG_WARNING("Game link: the game can't be sent '{0}', since the entity has no guid", transaction.Label);
				continue;
			}

			// Added, deleted or brought back: the whole entity as it is now, or that it is gone
			if (edit.bAddedOrDeleted)
			{
				if (bAlive)
				{
					if (std::optional<Nyx::Engine::CreateEntityMessage> message = Nyx::Engine::MakeCreateEntityMessage(world, edit.Handle))
					{
						Keep(Nyx::Engine::MakeNetMessage(*message));
					}
				}
				else if (edit.Guid.IsValid())
				{
					Keep(Nyx::Engine::MakeNetMessage(Nyx::Engine::DeleteEntityMessage{ edit.Guid }));
				}

				continue;
			}

			if (!bAlive)
			{
				continue;
			}

			for (const auto& [ops, propertyIndices] : GetChangedComponentProperties(world, edit))
			{
				if (std::optional<Nyx::Engine::SetPropertiesMessage> message =
						Nyx::Engine::MakeSetPropertiesMessage(world, edit.Handle, *ops, propertyIndices))
				{
					Keep(Nyx::Engine::MakeNetMessage(*message));
				}
			}
		}
	}
}

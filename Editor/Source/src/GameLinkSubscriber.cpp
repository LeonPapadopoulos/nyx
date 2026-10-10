#include "GameLinkSubscriber.h"

#include "ComponentTypeRegistry.h"
#include "EditorLinkMessages.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "SceneDocument.h"
#include "TransactionObjectRefHelpers.h"

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
		// As the undo step names it; the entity may be gone afterwards, or in another slot
		EntityGuid Guid;

		// Added, deleted, or brought back by undo or redo
		bool bAddedOrDeleted = false;

		// By component. An edit of a struct's field counts as an edit of the component's property
		// that holds the struct, which is sent whole.
		PropertiesByType<ComponentTypeOps> ChangedProperties;
	};

	EntityEdit& FindOrAddEdit(std::vector<EntityEdit>& edits, const ObjectRef& target)
	{
		const EntityGuid guid = GetSceneEntityGuid(target);

		for (EntityEdit& edit : edits)
		{
			if (edit.Guid == guid)
			{
				return edit;
			}
		}

		EntityEdit& edit = edits.emplace_back();
		edit.Guid = guid;
		return edit;
	}

	// The component a change is in, and its property that changed, or that holds the struct
	// whose field changed (from the change's Location)
	std::optional<std::pair<const ComponentTypeOps*, size_t>> FindChangedComponentProperty(const SetValueChange& change)
	{
		const SubobjectPath& location = change.Location;
		const bool bInStruct = location.SubobjectType && !location.PropertyIndices.empty();
		const TypeMetadata* componentType = bInStruct ? location.SubobjectType : change.TypeMetadata;
		const size_t propertyIndex = bInStruct ? location.PropertyIndices.front() : change.PropertyIndex;

		const ComponentTypeOps* ops = componentType ? Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(*componentType) : nullptr;
		if (!ops)
		{
			return std::nullopt;
		}

		return std::pair<const ComponentTypeOps*, size_t>{ ops, propertyIndex };
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

	void GameLinkSubscriber::OnTransactionPreviewed(const Transaction& transaction)
	{
		// The values as they are now, like after an edit; the drag's end sends them once more
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
				const auto changed = FindChangedComponentProperty(*setValue);
				if (GetSceneEntityGuid(setValue->Target).IsValid() && changed)
				{
					AddProperties(FindOrAddEdit(edits, setValue->Target).ChangedProperties, changed->first, { changed->second });
				}
			}
			else if (const AddObjectChange* addObject = std::get_if<AddObjectChange>(&change.Payload))
			{
				if (GetSceneEntityGuid(addObject->Target).IsValid())
				{
					FindOrAddEdit(edits, addObject->Target).bAddedOrDeleted = true;
				}
			}
			else if (const DeleteObjectChange* deleteObject = std::get_if<DeleteObjectChange>(&change.Payload))
			{
				if (GetSceneEntityGuid(deleteObject->Target).IsValid())
				{
					FindOrAddEdit(edits, deleteObject->Target).bAddedOrDeleted = true;
				}
			}
		}

		const Nyx::Engine::Registry& world = Scene.GetRegistry();

		for (const EntityEdit& edit : edits)
		{
			// Where the entity is now, if it is there at all
			const std::optional<Entity> entity = Scene.FindEntity(edit.Guid);

			// Added, deleted or brought back: the whole entity as it is now, or that it is gone
			if (edit.bAddedOrDeleted)
			{
				if (entity)
				{
					if (std::optional<Nyx::Engine::CreateEntityMessage> message = Nyx::Engine::MakeCreateEntityMessage(world, *entity))
					{
						Keep(Nyx::Engine::MakeNetMessage(*message));
					}
				}
				else
				{
					Keep(Nyx::Engine::MakeNetMessage(Nyx::Engine::DeleteEntityMessage{ edit.Guid }));
				}

				continue;
			}

			if (!entity)
			{
				continue;
			}

			for (const auto& [ops, propertyIndices] : edit.ChangedProperties)
			{
				if (std::optional<Nyx::Engine::SetPropertiesMessage> message =
						Nyx::Engine::MakeSetPropertiesMessage(world, *entity, *ops, propertyIndices))
				{
					Keep(Nyx::Engine::MakeNetMessage(*message));
				}
			}
		}
	}
}

#include "ComponentPostLoadSubscriber.h"

#include "ComponentTypeRegistry.h"
#include "SceneDocument.h"
#include "TransactionObjectRefHelpers.h"

#include <algorithm>
#include <variant>
#include <vector>

namespace Nyx::Editor
{
	ComponentPostLoadSubscriber::ComponentPostLoadSubscriber(Nyx::SceneDocument& scene)
		: Scene(scene)
	{
	}

	void ComponentPostLoadSubscriber::OnTransactionCommitted(const Transaction& transaction)
	{
		RunPostLoad(transaction);
	}

	void ComponentPostLoadSubscriber::OnTransactionApplied(const Transaction& transaction, bool /*bWasUndo*/)
	{
		RunPostLoad(transaction);
	}

	void ComponentPostLoadSubscriber::RunPostLoad(const Transaction& transaction)
	{
		// Each entity once, also if the transaction changed it several times
		std::vector<Nyx::Engine::EntityGuid> guids;
		for (const Change& change : transaction.Changes)
		{
			const ObjectRef target = std::visit(
				[](const auto& payload)
				{
					return payload.Target;
				},
				change.Payload);

			const Nyx::Engine::EntityGuid guid = GetSceneEntityGuid(target);
			if (guid.IsValid() && std::find(guids.begin(), guids.end(), guid) == guids.end())
			{
				guids.push_back(guid);
			}
		}

		Nyx::Engine::Registry& world = Scene.GetRegistry();
		for (const Nyx::Engine::EntityGuid guid : guids)
		{
			// Deleted entities have nothing to load
			const std::optional<Nyx::Engine::Entity> entity = Scene.FindEntity(guid);
			if (!entity)
			{
				continue;
			}

			for (const Nyx::Engine::ComponentTypeOps& ops : Nyx::Engine::ComponentTypeRegistry::Get().GetAll())
			{
				if (!ops.PostLoad)
				{
					continue;
				}

				if (void* component = ops.Get(world, *entity))
				{
					ops.PostLoad(component, PostLoadContext);
				}
			}
		}
	}
}

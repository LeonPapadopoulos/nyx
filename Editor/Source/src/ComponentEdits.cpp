#include "ComponentEdits.h"

#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "NameComponent.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"

namespace Nyx::Editor
{
	bool CanAddOrRemoveComponent(const Nyx::Engine::ComponentTypeOps& componentType)
	{
		return componentType.TypeMetadata != &Nyx::Reflection::GetTypeMetadata<Nyx::Engine::GuidComponent>() &&
			componentType.TypeMetadata != &Nyx::Reflection::GetTypeMetadata<Nyx::Engine::NameComponent>();
	}

	bool AddComponent(Nyx::SceneDocument& scene, TransactionSystem& transactions, Nyx::Engine::Entity entity,
		const Nyx::Engine::ComponentTypeOps& componentType)
	{
		Nyx::Engine::Registry& world = scene.GetRegistry();
		const ObjectRef target = MakeSceneEntityRef(scene, entity);
		if (!target.IsValid() || !CanAddOrRemoveComponent(componentType) || componentType.Has(world, entity))
		{
			return false;
		}

		void* component = componentType.Add(world, entity);

		Transaction transaction;
		transaction.Label = std::string("Add ") + componentType.TypeMetadata->DisplayName;
		transaction.Changes.push_back(Change{ EChangeKind::AddSubobject,
			AddSubobjectChange{ .Target = target, .AfterAdd = CaptureSubobjectSnapshot(component, *componentType.TypeMetadata) } });
		transactions.Push(std::move(transaction));
		return true;
	}

	bool RemoveComponent(Nyx::SceneDocument& scene, TransactionSystem& transactions, Nyx::Engine::Entity entity,
		const Nyx::Engine::ComponentTypeOps& componentType)
	{
		Nyx::Engine::Registry& world = scene.GetRegistry();
		const ObjectRef target = MakeSceneEntityRef(scene, entity);
		void* component = componentType.Get(world, entity);
		if (!target.IsValid() || !CanAddOrRemoveComponent(componentType) || !component)
		{
			return false;
		}

		// Captured before it is gone, so undo can bring it back as it was
		SubobjectSnapshot beforeRemove = CaptureSubobjectSnapshot(component, *componentType.TypeMetadata);
		componentType.Remove(world, entity);

		Transaction transaction;
		transaction.Label = std::string("Remove ") + componentType.TypeMetadata->DisplayName;
		transaction.Changes.push_back(
			Change{ EChangeKind::RemoveSubobject, RemoveSubobjectChange{ .Target = target, .BeforeRemove = std::move(beforeRemove) } });
		transactions.Push(std::move(transaction));
		return true;
	}
}

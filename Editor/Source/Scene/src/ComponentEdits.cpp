#include "ComponentEdits.h"

#include "AssetReference.h"
#include "ComponentTypeRegistry.h"
#include "MeshRendererComponent.h"
#include "TransactionDiffUtil.h"
#include "GuidComponent.h"
#include "NameComponent.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"

namespace Nyx::Editor
{
	bool CanAddOrRemoveComponent(const Nyx::ComponentTypeOps& componentType)
	{
		return !componentType.IsRuntime() && componentType.TypeMetadata != &Nyx::Reflection::GetTypeMetadata<Nyx::GuidComponent>() &&
			componentType.TypeMetadata != &Nyx::Reflection::GetTypeMetadata<Nyx::NameComponent>();
	}

	bool AddComponent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity,
		const Nyx::ComponentTypeOps& componentType)
	{
		Nyx::Registry& world = scene.GetRegistry();
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

	bool RemoveComponent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity,
		const Nyx::ComponentTypeOps& componentType)
	{
		Nyx::Registry& world = scene.GetRegistry();
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

	bool AssignMaterial(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity, const std::string& path)
	{
		Nyx::Registry& world = scene.GetRegistry();
		if (!world.IsAlive(entity) || !world.Has<Nyx::MeshRendererComponent>(entity))
		{
			return false;
		}
		Nyx::MeshRendererComponent& meshRenderer = world.Get<Nyx::MeshRendererComponent>(entity);
		if (meshRenderer.Material.Type == "Material" && meshRenderer.Material.Path == path)
		{
			return false;
		}

		// The reference is a struct inside the component: where it is, so undo finds it
		const Nyx::Reflection::TypeMetadata& componentType = Nyx::Reflection::GetTypeMetadata<Nyx::MeshRendererComponent>();
		SubobjectPath location;
		location.SubobjectType = &componentType;
		for (size_t index = 0; index < componentType.PropertyCount; ++index)
		{
			if (std::string_view(componentType.Properties[index].Name) == "Material")
			{
				location.PropertyIndices.push_back(index);
			}
		}
		TransactionDiffUtil diff;
		diff.TakeSnapshot(MakeSceneEntityRef(scene, entity), &meshRenderer.Material, Nyx::Reflection::GetTypeMetadata<Nyx::AssetReference>(), location);
		meshRenderer.Material.Type = "Material";
		meshRenderer.Material.Path = path;
		return diff.CommitChanges("Assign Material", transactions);
	}
}

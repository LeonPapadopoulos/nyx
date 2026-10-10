#include "SceneEntityTransactionDomain.h"

#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "TransactionObjectRefHelpers.h"

namespace
{
	// The live entity an undo step names, by its guid
	std::optional<Nyx::Engine::Entity> FindEntity(Nyx::Editor::EditorTransactionContext& context, const Nyx::Editor::ObjectRef& root)
	{
		if (!context.ActiveScene || root.Domain != Nyx::Editor::EObjectDomain::SceneEntity)
		{
			return std::nullopt;
		}

		return context.ActiveScene->FindEntity(Nyx::Editor::GetSceneEntityGuid(root));
	}
}

namespace Nyx::Editor
{
	void* SceneEntityTransactionDomain::ResolveMutable(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		const std::optional<Nyx::Engine::Entity> entity = FindEntity(context, root);
		if (!entity)
		{
			return nullptr;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return nullptr;
		}

		return ops->Get(context.ActiveScene->GetRegistry(), *entity);
	}

	bool SceneEntityTransactionDomain::CreateRootObject(
		EditorTransactionContext& context,
		const ObjectRef& root)
	{
		const Nyx::Engine::EntityGuid guid = GetSceneEntityGuid(root);
		if (!context.ActiveScene || !guid.IsValid())
		{
			return false;
		}

		// Already there: undo and redo got out of step with the scene, and a second entity with
		// the same guid would break everything that finds entities by guid
		if (context.ActiveScene->FindEntity(guid))
		{
			return false;
		}

		// Any free slot will do; the old one may belong to another entity by now. The snapshot
		// restored next brings the other components back.
		auto& world = context.ActiveScene->GetRegistry();
		const Nyx::Engine::Entity entity = world.CreateEntity();
		world.Add<Nyx::Engine::GuidComponent>(entity, Nyx::Engine::GuidComponent{ guid });
		return true;
	}

	bool SceneEntityTransactionDomain::DeleteRootObject(
		EditorTransactionContext& context,
		const ObjectRef& root)
	{
		const std::optional<Nyx::Engine::Entity> entity = FindEntity(context, root);
		if (!entity)
		{
			return false;
		}

		// Through the scene, so the selection doesn't keep the deleted entity
		return context.ActiveScene->DestroyEntity(*entity);
	}

	void SceneEntityTransactionDomain::EnumerateSubobjects(
		EditorTransactionContext& context,
		const ObjectRef& root,
		std::vector<ReflectedObjectView>& outSubobjects)
	{
		outSubobjects.clear();

		const std::optional<Nyx::Engine::Entity> entity = FindEntity(context, root);
		if (!entity)
		{
			return;
		}

		auto& world = context.ActiveScene->GetRegistry();
		for (const Nyx::Engine::ComponentTypeOps& ops : Nyx::Engine::ComponentTypeRegistry::Get().GetAll())
		{
			void* object = ops.Get(world, *entity);
			if (!object)
			{
				continue;
			}

			outSubobjects.push_back(ReflectedObjectView{
				.TypeMetadata = ops.TypeMetadata,
				.Object = object });
		}
	}

	bool SceneEntityTransactionDomain::EnsureSubobject(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		const std::optional<Nyx::Engine::Entity> entity = FindEntity(context, root);
		if (!entity)
		{
			return false;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return false;
		}

		ops->Add(context.ActiveScene->GetRegistry(), *entity);
		return true;
	}

	bool SceneEntityTransactionDomain::RemoveSubobject(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		const std::optional<Nyx::Engine::Entity> entity = FindEntity(context, root);
		if (!entity)
		{
			return false;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return false;
		}

		ops->Remove(context.ActiveScene->GetRegistry(), *entity);
		return true;
	}
}

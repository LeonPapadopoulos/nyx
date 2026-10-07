#include "SceneEntityTransactionDomain.h"

#include "ComponentTypeRegistry.h"

namespace Nyx::Editor
{
	void* SceneEntityTransactionDomain::ResolveMutable(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return nullptr;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		if (!world.IsAlive(entity))
		{
			return nullptr;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return nullptr;
		}

		return ops->Get(world, entity);
	}

	bool SceneEntityTransactionDomain::CreateRootObject(
		EditorTransactionContext& context,
		const ObjectRef& root)
	{
		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		return world.RestoreEntity(entity);
	}

	bool SceneEntityTransactionDomain::DeleteRootObject(
		EditorTransactionContext& context,
		const ObjectRef& root)
	{
		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		if (!world.IsAlive(entity))
		{
			return false;
		}

		return world.DestroyEntity(entity);
	}

	void SceneEntityTransactionDomain::EnumerateSubobjects(
		EditorTransactionContext& context,
		const ObjectRef& root,
		std::vector<ReflectedObjectView>& outSubobjects)
	{
		outSubobjects.clear();

		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		if (!world.IsAlive(entity))
		{
			return;
		}

		for (const Nyx::Engine::ComponentTypeOps& ops : Nyx::Engine::ComponentTypeRegistry::Get().GetAll())
		{
			void* object = ops.Get(world, entity);
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
		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		if (!world.IsAlive(entity))
		{
			return false;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return false;
		}

		ops->Add(world, entity);
		return true;
	}

	bool SceneEntityTransactionDomain::RemoveSubobject(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!context.ActiveScene || root.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(root.Id.Value);

		if (!world.IsAlive(entity))
		{
			return false;
		}

		const Nyx::Engine::ComponentTypeOps* ops =
			Nyx::Engine::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops)
		{
			return false;
		}

		ops->Remove(world, entity);
		return true;
	}
}
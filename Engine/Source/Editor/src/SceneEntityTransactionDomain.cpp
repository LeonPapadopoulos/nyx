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

		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(&typeMetadata);
		if (!ops || !ops->GetMutable)
		{
			return nullptr;
		}

		return ops->GetMutable(world, entity);
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

		for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
		{
			if (!ops.TypeMetadata || !ops.Has || !ops.GetMutable)
			{
				continue;
			}

			if (!ops.Has(world, entity))
			{
				continue;
			}

			void* object = ops.GetMutable(world, entity);
			if (!object)
			{
				continue;
			}

			outSubobjects.push_back(ReflectedObjectView{
				.TypeMetadata = ops.TypeMetadata,
				.Object = object
				});
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

		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(&typeMetadata);
		if (!ops || !ops->AddDefault)
		{
			return false;
		}

		ops->AddDefault(world, entity);
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

		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(&typeMetadata);
		if (!ops || !ops->RemoveIfPresent)
		{
			return false;
		}

		ops->RemoveIfPresent(world, entity);
		return true;
	}
}
#include "SceneEntityTransactionDomain.h"

#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "TransformComponent.h"

#include <string_view>

namespace Nyx::Editor
{
	void* SceneEntityTransactionDomain::ResolveMutable(
		EditorTransactionContext& context,
		const ObjectRef& objectRef,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!context.ActiveScene || objectRef.Domain != EObjectDomain::SceneEntity)
		{
			return nullptr;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(objectRef.Id.Value);

		if (!world.IsAlive(entity))
		{
			return nullptr;
		}

		const std::string_view typeName = typeMetadata.Name ? typeMetadata.Name : "";

		if (typeName == "Nyx::Engine::TransformComponent")
		{
			if (!world.Has<Nyx::Engine::TransformComponent>(entity))
			{
				return nullptr;
			}
			return &world.Get<Nyx::Engine::TransformComponent>(entity);
		}

		if (typeName == "Nyx::Engine::NameComponent")
		{
			if (!world.Has<Nyx::Engine::NameComponent>(entity))
			{
				return nullptr;
			}
			return &world.Get<Nyx::Engine::NameComponent>(entity);
		}

		if (typeName == "Nyx::Engine::MeshRendererComponent")
		{
			if (!world.Has<Nyx::Engine::MeshRendererComponent>(entity))
			{
				return nullptr;
			}
			return &world.Get<Nyx::Engine::MeshRendererComponent>(entity);
		}

		return nullptr;
	}

	bool SceneEntityTransactionDomain::CreateObject(
		EditorTransactionContext& context,
		const AddObjectChange& change)
	{
		if (!context.ActiveScene || change.Target.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		if (!std::holds_alternative<SceneEntitySnapshot>(change.AfterCreate))
		{
			return false;
		}

		auto& scene = *context.ActiveScene;
		auto& world = scene.GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(change.Target.Id.Value);

		if (!world.RestoreEntity(entity))
		{
			return false;
		}

		const SceneEntitySnapshot& snapshot = std::get<SceneEntitySnapshot>(change.AfterCreate);
		RestoreSnapshot(scene, entity, snapshot);

		return true;
	}

	bool SceneEntityTransactionDomain::DeleteObject(
		EditorTransactionContext& context,
		const DeleteObjectChange& change)
	{
		if (!context.ActiveScene || change.Target.Domain != EObjectDomain::SceneEntity)
		{
			return false;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(change.Target.Id.Value);

		if (!world.IsAlive(entity))
		{
			return false;
		}

		return world.DestroyEntity(entity);
	}

	SceneEntitySnapshot SceneEntityTransactionDomain::CaptureSnapshot(
		Nyx::SceneDocument& scene,
		Nyx::Engine::Entity entity) const
	{
		SceneEntitySnapshot snapshot{};
		auto& world = scene.GetRegistry();

		snapshot.bAlive = world.IsAlive(entity);
		if (!snapshot.bAlive)
		{
			return snapshot;
		}

		if (world.Has<Nyx::Engine::NameComponent>(entity))
		{
			snapshot.Name = world.Get<Nyx::Engine::NameComponent>(entity);
		}

		if (world.Has<Nyx::Engine::TransformComponent>(entity))
		{
			snapshot.Transform = world.Get<Nyx::Engine::TransformComponent>(entity);
		}

		if (world.Has<Nyx::Engine::MeshRendererComponent>(entity))
		{
			snapshot.MeshRenderer = world.Get<Nyx::Engine::MeshRendererComponent>(entity);
		}

		return snapshot;
	}

	void SceneEntityTransactionDomain::RestoreSnapshot(
		Nyx::SceneDocument& scene,
		Nyx::Engine::Entity entity,
		const SceneEntitySnapshot& snapshot) const
	{
		auto& world = scene.GetRegistry();

		if (snapshot.Name.has_value())
		{
			if (world.Has<Nyx::Engine::NameComponent>(entity))
			{
				world.Get<Nyx::Engine::NameComponent>(entity) = snapshot.Name.value();
			}
			else
			{
				world.Add<Nyx::Engine::NameComponent>(entity, snapshot.Name.value());
			}
		}

		if (snapshot.Transform.has_value())
		{
			if (world.Has<Nyx::Engine::TransformComponent>(entity))
			{
				world.Get<Nyx::Engine::TransformComponent>(entity) = snapshot.Transform.value();
			}
			else
			{
				world.Add<Nyx::Engine::TransformComponent>(entity, snapshot.Transform.value());
			}
		}

		if (snapshot.MeshRenderer.has_value())
		{
			if (world.Has<Nyx::Engine::MeshRendererComponent>(entity))
			{
				world.Get<Nyx::Engine::MeshRendererComponent>(entity) = snapshot.MeshRenderer.value();
			}
			else
			{
				world.Add<Nyx::Engine::MeshRendererComponent>(entity, snapshot.MeshRenderer.value());
			}
		}
	}
}
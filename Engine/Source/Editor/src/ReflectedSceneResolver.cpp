#include "ReflectedSceneResolver.h"

#include <string_view>

namespace Nyx::Editor
{
	void* ReflectedSceneResolver::ResolveMutable(
		EditorTransactionContext& context,
		InspectorTargetId targetId,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!context.ActiveScene)
		{
			return nullptr;
		}

		auto& world = context.ActiveScene->GetRegistry();

		Nyx::Engine::Entity entity{};
		entity.Value = static_cast<decltype(entity.Value)>(targetId.Value);

		if (!world.IsAlive(entity))
		{
			return nullptr;
		}

		if (std::string_view(typeMetadata.Name) == "Nyx::Engine::TransformComponent")
		{
			if (!world.Has<Nyx::Engine::TransformComponent>(entity))
			{
				return nullptr;
			}

			return &world.Get<Nyx::Engine::TransformComponent>(entity);
		}

		return nullptr;
	}
}
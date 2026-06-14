#pragma once

#include "Entity.h"
#include "ReflectionTypes.h"

#include <vector>

namespace Nyx::Engine
{
	class Registry;
}

namespace Nyx::Editor
{
	struct ComponentTypeOps
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;

		bool (*Has)(const Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
		void* (*GetMutable)(Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
		void (*AddDefault)(Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
		void (*RemoveIfPresent)(Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
	};

	class ComponentTypeRegistry
	{
	public:
		static ComponentTypeRegistry& Get();

		void Register(ComponentTypeOps ops);

		const std::vector<ComponentTypeOps>& GetAll() const
		{
			return Types;
		}

		const ComponentTypeOps* FindByTypeMetadata(const Nyx::Reflection::TypeMetadata* typeMetadata) const;

	private:
		std::vector<ComponentTypeOps> Types;
	};

	template<typename TComponent>
	ComponentTypeOps MakeComponentTypeOps()
	{
		ComponentTypeOps ops{};
		ops.TypeMetadata = &Nyx::Reflection::GetTypeMetadata<TComponent>();

		ops.Has =
			[](const Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity) -> bool
			{
				return registry.Has<TComponent>(entity);
			};

		ops.GetMutable =
			[](Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity) -> void*
			{
				if (!registry.Has<TComponent>(entity))
				{
					return nullptr;
				}
				return &registry.Get<TComponent>(entity);
			};

		ops.AddDefault =
			[](Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity)
			{
				if (!registry.Has<TComponent>(entity))
				{
					registry.Add<TComponent>(entity, TComponent{});
				}
			};

		ops.RemoveIfPresent =
			[](Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity)
			{
				if (registry.Has<TComponent>(entity))
				{
					registry.Remove<TComponent>(entity);
				}
			};

		return ops;
	}
}
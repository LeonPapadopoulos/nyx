#pragma once

#include "Assertions.h"
#include "Entity.h"
#include "ReflectionTypes.h"
#include "SceneSerializationTypes.h"

#include <functional>
#include <string_view>
#include <vector>

namespace Nyx::Engine
{
	// Access to one component type without knowing its C++ type. Lets generic code, such as
	// the scene serializer or the editor's details panel, work with every component type.
	struct ComponentTypeOps
	{
		// Name, display name and properties of the type. The name is what scene files store.
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;

		bool (*Has)(const Registry& registry, Entity entity) = nullptr;

		// Both return nullptr if the entity has no such component.
		void* (*Get)(Registry& registry, Entity entity) = nullptr;
		const void* (*GetConst)(const Registry& registry, Entity entity) = nullptr;

		// Adds a default component unless the entity already has one, and returns the component.
		void* (*Add)(Registry& registry, Entity entity) = nullptr;

		// Removes the component if the entity has one.
		void (*Remove)(Registry& registry, Entity entity) = nullptr;

		// Optional: runs after the component was loaded from a scene file,
		// for example to look up the assets it references.
		std::function<void(void* component, ScenePostLoadContext& context)> PostLoad;
	};

	// Every component type of the engine. Filled once at startup by RegisterComponentTypes().
	class ComponentTypeRegistry
	{
	public:
		static ComponentTypeRegistry& Get();

		// Adds component type T. Called by the generated reflection code for every
		// NYX_REFLECT(Component) type; registering a type twice has no effect.
		template<typename T>
		void Register();

		// Sets the step that runs after a component of type T was loaded from a scene file.
		template<typename T>
		void SetPostLoad(void (*postLoad)(T& component, ScenePostLoadContext& context));

		const std::vector<ComponentTypeOps>& GetAll() const
		{
			return Types;
		}

		const ComponentTypeOps* FindByTypeMetadata(const Nyx::Reflection::TypeMetadata& typeMetadata) const;
		const ComponentTypeOps* FindByName(std::string_view name) const;

	private:
		std::vector<ComponentTypeOps> Types;
	};

	template<typename T>
	void ComponentTypeRegistry::Register()
	{
		const Nyx::Reflection::TypeMetadata& typeMetadata = Nyx::Reflection::GetTypeMetadata<T>();
		if (FindByTypeMetadata(typeMetadata))
		{
			return;
		}

		ComponentTypeOps ops;
		ops.TypeMetadata = &typeMetadata;

		ops.Has = [](const Registry& registry, Entity entity) -> bool
		{
			return registry.Has<T>(entity);
		};

		ops.Get = [](Registry& registry, Entity entity) -> void*
		{
			return registry.Has<T>(entity) ? &registry.Get<T>(entity) : nullptr;
		};

		ops.GetConst = [](const Registry& registry, Entity entity) -> const void*
		{
			return registry.Has<T>(entity) ? &registry.Get<T>(entity) : nullptr;
		};

		ops.Add = [](Registry& registry, Entity entity) -> void*
		{
			if (registry.Has<T>(entity))
			{
				return &registry.Get<T>(entity);
			}

			return &registry.Add<T>(entity, T{});
		};

		ops.Remove = [](Registry& registry, Entity entity)
		{
			if (registry.Has<T>(entity))
			{
				registry.Remove<T>(entity);
			}
		};

		Types.push_back(std::move(ops));
	}

	template<typename T>
	void ComponentTypeRegistry::SetPostLoad(void (*postLoad)(T& component, ScenePostLoadContext& context))
	{
		const Nyx::Reflection::TypeMetadata& typeMetadata = Nyx::Reflection::GetTypeMetadata<T>();

		for (ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == &typeMetadata)
			{
				ops.PostLoad = [postLoad](void* component, ScenePostLoadContext& context)
				{
					postLoad(*static_cast<T*>(component), context);
				};
				return;
			}
		}

		ASSERT(false && "Register the component type before setting its post-load step.");
	}
}

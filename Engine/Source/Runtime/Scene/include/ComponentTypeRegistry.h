#pragma once

#include "Assertions.h"
#include "Entity.h"
#include "ReflectionTypes.h"
#include "SceneSerializationTypes.h"

#include <functional>
#include <string_view>
#include <vector>

namespace Nyx
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

		// Removes the component if the entity has one, then runs the type's remove step, if any
		// (SetOnRemove())
		std::function<void(Registry& registry, Entity entity)> Remove;

		// Optional: runs after the component was loaded from a scene file, added or edited, in the
		// editor and in a game, for example to look up the assets it references, or to make what is
		// derived from it on its entity, such as a generated mesh (ScenePostLoadContext says what it
		// may change)
		std::function<void(void* component, ScenePostLoadContext& context)> PostLoad;
	};

	// Every component type of the engine. Filled once at startup by RegisterComponentTypes().
	class ComponentTypeRegistry
	{
	public:
		static ComponentTypeRegistry& Get();

		// Adds component type T. Called by the generated reflection code for every
		// NYX_REFLECT(Component) type; registering a type twice has no effect. A different type of
		// the same name, e.g. a game module's type named like one of the engine's, is refused with
		// an error, and the first one is kept.
		template<typename T>
		void Register();

		// Sets the step that runs after a component of type T was loaded from a scene file.
		template<typename T>
		void SetPostLoad(void (*postLoad)(T& component, ScenePostLoadContext& context));

		// Sets the step that runs after a component of type T was removed through its ops, e.g. by
		// the editor or undo: to take away what its post-load step made
		template<typename T>
		void SetOnRemove(void (*onRemove)(Registry& registry, Entity entity));

		const std::vector<ComponentTypeOps>& GetAll() const
		{
			return Types;
		}

		const ComponentTypeOps* FindByTypeMetadata(const Nyx::Reflection::TypeMetadata& typeMetadata) const;
		const ComponentTypeOps* FindByName(std::string_view name) const;

	private:
		// False if the type is registered already, or another type has its name (logged as an error)
		bool CanRegister(const Nyx::Reflection::TypeMetadata& typeMetadata) const;

		std::vector<ComponentTypeOps> Types;
	};

	template<typename T>
	void ComponentTypeRegistry::Register()
	{
		const Nyx::Reflection::TypeMetadata& typeMetadata = Nyx::Reflection::GetTypeMetadata<T>();
		if (!CanRegister(typeMetadata))
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

	template<typename T>
	void ComponentTypeRegistry::SetOnRemove(void (*onRemove)(Registry& registry, Entity entity))
	{
		const Nyx::Reflection::TypeMetadata& typeMetadata = Nyx::Reflection::GetTypeMetadata<T>();

		for (ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == &typeMetadata)
			{
				ops.Remove = [onRemove](Registry& registry, Entity entity)
				{
					if (registry.Has<T>(entity))
					{
						registry.Remove<T>(entity);
						onRemove(registry, entity);
					}
				};
				return;
			}
		}

		ASSERT(false && "Register the component type before setting its remove step.");
	}
}

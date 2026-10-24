#pragma once

#include "Assertions.h"
#include "ComponentInspection.h"
#include "Entity.h"
#include "ReflectionTypes.h"
#include "SceneSerializationTypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	enum class EComponentTypeFlags : uint8_t
	{
		None = 0,

		// Made while the world runs and never saved, e.g. a mesh made at run time or a system's own
		// state: scene files (and with them copies and the editor link's CreateEntity), undo's
		// snapshots, Add Component and the Details panel's editable components leave it out. The
		// Details panel shows it greyed in its Runtime section, from a running game while Play runs
		// (InspectRuntimeComponents()).
		Runtime = 1 << 0,
	};

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

		EComponentTypeFlags Flags = EComponentTypeFlags::None;

		// Optional: what the component is as names and values, e.g. { "Revision", "3" }, for what
		// reflection doesn't show; the Details panel's Runtime section shows them
		std::function<std::vector<InspectedValue>(const void* component)> Describe;

		bool IsRuntime() const
		{
			return (static_cast<uint8_t>(Flags) & static_cast<uint8_t>(EComponentTypeFlags::Runtime)) != 0;
		}
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

		// Makes a registered type (a reflected one, which the header tool registered) a run-time
		// type: never saved; its Serialize properties are what the editor is shown
		// (EComponentTypeFlags::Runtime). A game module calls it in its RegisterTypes().
		template<typename T>
		void SetRuntime();

		// Registers a run-time type that isn't reflected, e.g. one holding a pointer to its data: it
		// gets metadata of its own (the name and display name, no properties) and is never saved.
		// describe, if any, is its describe step (SetDescribe()).
		template<typename T>
		void RegisterRuntime(std::string name, std::string displayName, std::vector<InspectedValue> (*describe)(const T& component) = nullptr);

		// Sets what a component of a registered reflected type T says it is as names and values
		// (ComponentTypeOps::Describe)
		template<typename T>
		void SetDescribe(std::vector<InspectedValue> (*describe)(const T& component));

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

		// Whether RegisterRuntime() registered a type of this name
		bool IsOwnRuntimeType(std::string_view name) const;

		// A registered type's ops, by its metadata; asserts that it is registered
		ComponentTypeOps* FindRegistered(const Nyx::Reflection::TypeMetadata& typeMetadata, const char* what);

		// The ops of a component type T with this metadata
		template<typename T>
		static ComponentTypeOps MakeOps(const Nyx::Reflection::TypeMetadata& typeMetadata);

		template<typename T>
		static std::function<std::vector<InspectedValue>(const void*)> MakeDescribe(std::vector<InspectedValue> (*describe)(const T& component));

		std::vector<ComponentTypeOps> Types;

		// The metadata RegisterRuntime() makes, and its names; deques, so they never move
		std::deque<Nyx::Reflection::TypeMetadata> OwnMetadata;
		std::deque<std::string> OwnNames;
	};

	template<typename T>
	void ComponentTypeRegistry::Register()
	{
		const Nyx::Reflection::TypeMetadata& typeMetadata = Nyx::Reflection::GetTypeMetadata<T>();
		if (!CanRegister(typeMetadata))
		{
			return;
		}

		Types.push_back(MakeOps<T>(typeMetadata));
	}

	template<typename T>
	void ComponentTypeRegistry::SetRuntime()
	{
		if (ComponentTypeOps* ops = FindRegistered(Nyx::Reflection::GetTypeMetadata<T>(), "making it a run-time type"))
		{
			ops->Flags = EComponentTypeFlags::Runtime;
		}
	}

	template<typename T>
	void ComponentTypeRegistry::RegisterRuntime(std::string name, std::string displayName, std::vector<InspectedValue> (*describe)(const T& component))
	{
		// Registering it again has no effect, as for a reflected type
		if (IsOwnRuntimeType(name))
		{
			return;
		}

		OwnNames.push_back(std::move(name));
		const char* typeName = OwnNames.back().c_str();
		OwnNames.push_back(std::move(displayName));
		const char* typeDisplayName = OwnNames.back().c_str();
		OwnMetadata.push_back(Nyx::Reflection::TypeMetadata{
			.Name = typeName, .DisplayName = typeDisplayName, .Role = Nyx::Reflection::EReflectedTypeRole::Component });
		const Nyx::Reflection::TypeMetadata& typeMetadata = OwnMetadata.back();
		if (!CanRegister(typeMetadata))
		{
			return;
		}

		Types.push_back(MakeOps<T>(typeMetadata));
		Types.back().Flags = EComponentTypeFlags::Runtime;
		if (describe)
		{
			Types.back().Describe = MakeDescribe<T>(describe);
		}
	}

	template<typename T>
	void ComponentTypeRegistry::SetDescribe(std::vector<InspectedValue> (*describe)(const T& component))
	{
		if (ComponentTypeOps* ops = FindRegistered(Nyx::Reflection::GetTypeMetadata<T>(), "setting its describe step"))
		{
			ops->Describe = MakeDescribe<T>(describe);
		}
	}

	template<typename T>
	std::function<std::vector<InspectedValue>(const void*)> ComponentTypeRegistry::MakeDescribe(std::vector<InspectedValue> (*describe)(const T& component))
	{
		return [describe](const void* component)
		{
			return describe(*static_cast<const T*>(component));
		};
	}

	template<typename T>
	ComponentTypeOps ComponentTypeRegistry::MakeOps(const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
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

		return ops;
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

#pragma once

#include "Entity.h"
#include "InspectorDrawContext.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionTypes.h"

#include <vector>

namespace Nyx::Engine
{
	class Registry;
}

namespace Nyx::Editor
{
	struct ComponentInspectorEntry
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		const char* DisplayName = "";

		bool (*Has)(const Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
		void* (*GetMutable)(Nyx::Engine::Registry&, Nyx::Engine::Entity) = nullptr;
		void (*Draw)(void* object, Nyx::Editor::InspectorDrawContext& drawContext) = nullptr;
	};

	class ComponentInspectorRegistry
	{
	public:
		static ComponentInspectorRegistry& Get();

		void Register(ComponentInspectorEntry entry);

		const std::vector<ComponentInspectorEntry>& GetAll() const
		{
			return Entries;
		}

	private:
		std::vector<ComponentInspectorEntry> Entries;
	};

	template<typename TComponent>
	ComponentInspectorEntry MakeReflectedComponentInspectorEntry(const char* displayName)
	{
		ComponentInspectorEntry entry{};
		entry.TypeMetadata = &Nyx::Reflection::GetTypeMetadata<TComponent>();
		entry.DisplayName = displayName;

		entry.Has =
			[](const Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity) -> bool
			{
				return registry.Has<TComponent>(entity);
			};

		entry.GetMutable =
			[](Nyx::Engine::Registry& registry, Nyx::Engine::Entity entity) -> void*
			{
				if (!registry.Has<TComponent>(entity))
				{
					return nullptr;
				}

				return &registry.Get<TComponent>(entity);
			};

		entry.Draw =
			[](void* object, Nyx::Editor::InspectorDrawContext& drawContext)
			{
				DrawReflectedTypeTable(
					object,
					Nyx::Reflection::GetTypeMetadata<TComponent>(),
					drawContext
				);
			};

		return entry;
	}
}
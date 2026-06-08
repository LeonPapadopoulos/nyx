#pragma once

#include "ComponentEditors.h"
#include "Entity.h"

#include <functional>
#include <vector>

namespace Nyx::Editor
{
	struct ComponentInspectorEntry
	{
		const char* DisplayName = "";
		std::function<bool(const Nyx::Engine::Registry&, Nyx::Engine::Entity)> HasComponent;
		std::function<void(Nyx::Engine::Registry&, Nyx::Engine::Entity, Nyx::Editor::InspectorDrawContext&)> DrawComponent;
	};

	template<typename T>
	ComponentInspectorEntry MakeInspectorEntry()
	{
		static_assert(ComponentEditorMeta<T>::Enabled, "Missing ComponentEditorMeta registration.");

		ComponentInspectorEntry entry{};
		entry.DisplayName = ComponentEditorMeta<T>::GetDisplayName();

		entry.HasComponent =
			[](const Nyx::Engine::Registry& world, Nyx::Engine::Entity entity) -> bool
			{
				return world.Has<T>(entity);
			};

		entry.DrawComponent =
			[](Nyx::Engine::Registry& world, Nyx::Engine::Entity entity, Nyx::Editor::InspectorDrawContext& drawContext)
			{
				T& component = world.Get<T>(entity);

				ImGui::PushID(ComponentEditorMeta<T>::GetDisplayName());

				if (ImGui::CollapsingHeader(ComponentEditorMeta<T>::GetDisplayName(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawProperties(&component, ComponentEditorMeta<T>::GetProperties());
					ComponentEditorMeta<T>::DrawExtra(component, drawContext);
				}

				ImGui::PopID();
			};

		return entry;
	}

	inline const std::vector<ComponentInspectorEntry>& GetDefaultComponentInspectors()
	{
		static const std::vector<ComponentInspectorEntry> Inspectors =
		{
			MakeInspectorEntry<Nyx::Engine::NameComponent>(),
			MakeInspectorEntry<Nyx::Engine::TransformComponent>(),
			MakeInspectorEntry<Nyx::Engine::MeshRendererComponent>()
		};

		return Inspectors;
	}
}
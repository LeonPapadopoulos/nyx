#pragma once

#include "ComponentEditorRegistration.h"
#include "InspectorDrawContext.h"
#include "MeshRendererComponent.h"
#include "EditableObjectRegistrations.h"

#include "Entity.h"
#include "NameComponent.h"
#include "TransformComponent.h"

#include "ReflectedPropertyDrawer.h"
#include "ReflectionTypes.h"

#include <glm/glm.hpp>

NYX_REGISTER_COMPONENT_EDITOR(
	Nyx::Engine::NameComponent,
	"Name",
	NYX_EDITOR_FIELD(Nyx::Engine::NameComponent, Name, String, "Name", 0.0f)
);

namespace Nyx::Editor
{
	template<>
	struct ComponentEditorMeta<Nyx::Engine::TransformComponent>
	{
		static constexpr bool Enabled = true;

		static const char* GetDisplayName()
		{
			return "Transform";
		}

		static const std::vector<EditorPropertyDesc>& GetProperties()
		{
			static const std::vector<EditorPropertyDesc> EmptyProperties{};
			return EmptyProperties;
		}

		static void DrawExtra(
			Nyx::Engine::TransformComponent& component,
			Nyx::Editor::InspectorDrawContext& drawContext)
		{
			const Nyx::Reflection::TypeMetadata& typeMetadata =
				Nyx::Reflection::GetTypeMetadata<Nyx::Engine::TransformComponent>();

			DrawReflectedTypeTable(&component, typeMetadata, drawContext);
		}
	};
}

namespace Nyx::Editor
{
	template<>
	struct ComponentEditorMeta<Nyx::Engine::MeshRendererComponent>
	{
		static constexpr bool Enabled = true;

		static const char* GetDisplayName()
		{
			return "Mesh Renderer";
		}

		static const std::vector<EditorPropertyDesc>& GetProperties()
		{
			static const std::vector<EditorPropertyDesc> Properties =
			{
				NYX_EDITOR_FIELD(Nyx::Engine::MeshRendererComponent, bVisible, Bool, "Visible", 0.0f)
			};

			return Properties;
		}

		static void DrawExtra(
			Nyx::Engine::MeshRendererComponent& component,
			Nyx::Editor::InspectorDrawContext& drawContext)
		{
			ImGui::Separator();
			ImGui::Text("Mesh: %p", static_cast<void*>(component.MeshAsset));
			ImGui::Text("Material: %p", static_cast<void*>(component.MaterialAsset));
		}
	};
}
#pragma once

#include "ComponentEditorRegistration.h"
#include "MeshRendererComponent.h"

#include "NameComponent.h"
#include "TransformComponent.h"

NYX_REGISTER_COMPONENT_EDITOR(
	Nyx::Engine::NameComponent,
	"Name",
	NYX_EDITOR_FIELD(Nyx::Engine::NameComponent, Name, String, "Name", 0.0f)
);

NYX_REGISTER_COMPONENT_EDITOR(
	Nyx::Engine::TransformComponent,
	"Transform",
	NYX_EDITOR_FIELD(Nyx::Engine::TransformComponent, Position, Vec3, "Position", 0.1f),
	NYX_EDITOR_FIELD(Nyx::Engine::TransformComponent, RotationRadians, Vec3, "Rotation", 0.01f),
	NYX_EDITOR_FIELD(Nyx::Engine::TransformComponent, Scale, Vec3, "Scale", 0.01f)
);

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

		static void DrawExtra(Nyx::Engine::MeshRendererComponent& component)
		{
			ImGui::Separator();
			ImGui::Text("Mesh: %p", static_cast<void*>(component.MeshAsset));
			ImGui::Text("Material: %p", static_cast<void*>(component.MaterialAsset));
		}
	};
}
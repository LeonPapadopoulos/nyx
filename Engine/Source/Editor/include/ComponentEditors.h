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

		static void DrawExtra(Nyx::Engine::TransformComponent& component)
		{
			if (ImGui::BeginTable("TransformProperties", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				// Position
				ImGui::PushID("Position");
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("Position");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::DragFloat3("##Field", &component.Position.x, 0.1f);
				}
				ImGui::PopID();

				// Rotation
				ImGui::PushID("Rotation");
				{
					glm::vec3 rotationDegrees = glm::degrees(component.GetRotationEulerRadians());

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("Rotation");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);

					if (ImGui::DragFloat3("##Field", &rotationDegrees.x, 1.0f))
					{
						component.SetRotationEulerRadians(glm::radians(rotationDegrees));
					}
				}
				ImGui::PopID();

				// Scale
				ImGui::PushID("Scale");
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("Scale");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::DragFloat3("##Field", &component.Scale.x, 0.01f);
				}
				ImGui::PopID();

				ImGui::EndTable();
			}
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

		static void DrawExtra(Nyx::Engine::MeshRendererComponent& component)
		{
			ImGui::Separator();
			ImGui::Text("Mesh: %p", static_cast<void*>(component.MeshAsset));
			ImGui::Text("Material: %p", static_cast<void*>(component.MaterialAsset));
		}
	};
}
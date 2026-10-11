#include "SceneOutlinerPanel.h"

#include "ImGuiSource.h"
#include "NameComponent.h"
#include "SceneEditor.h"

#include "imgui.h"

#include <string>

namespace Nyx::Editor
{
	void SceneOutlinerPanel::Draw(SceneEditor& editing)
	{
		if (!bOpen)
		{
			return;
		}

		if (!Nyx::UI::Begin("Scene Outliner", &bOpen))
		{
			ImGui::End();
			return;
		}

		auto& world = editing.Scene.GetRegistry();
		auto& selection = editing.Scene.GetSelection();

		if (NYX_UI(ImGui::Button("Add Entity")))
		{
			editing.CreateEntity();
		}

		ImGui::SameLine();

		const bool bHasSelection = selection.has_value();
		if (!bHasSelection)
		{
			ImGui::BeginDisabled();
		}

		if (NYX_UI(ImGui::Button("Delete Selected")) && bHasSelection)
		{
			editing.DeleteSelectedEntity();
		}

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Duplicate")) && bHasSelection)
		{
			editing.DuplicateSelectedEntity();
		}
		ImGui::SetItemTooltip("Ctrl+D. Ctrl+C and Ctrl+V copy and paste; right-click an entity for these too.");

		if (!bHasSelection)
		{
			ImGui::EndDisabled();
		}

		NYX_UI(ImGui::Separator());

		world.Each<Nyx::Engine::NameComponent>(
			[&](Nyx::Engine::Entity entity, const Nyx::Engine::NameComponent& name)
			{
				const bool bSelected = selection.has_value() && selection.value() == entity;

				std::string label = name.Name + "##" + std::to_string(entity.Index());

				if (NYX_UI(ImGui::Selectable(label.c_str(), bSelected)))
				{
					selection = entity;
				}

				// Right-click: selects the entity and offers what the hotkeys do
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					selection = entity;
					ImGui::OpenPopup("##EntityMenu");
				}
			});

		// After the list, so a new entity doesn't appear while the list is being drawn
		if (Nyx::UI::BeginPopup("##EntityMenu"))
		{
			if (NYX_UI(ImGui::MenuItem("Duplicate", "Ctrl+D")))
			{
				editing.DuplicateSelectedEntity();
			}
			if (NYX_UI(ImGui::MenuItem("Copy", "Ctrl+C")))
			{
				editing.CopySelectedEntity();
			}
			if (NYX_UI(ImGui::MenuItem("Paste", "Ctrl+V", false, editing.CanPaste())))
			{
				editing.PasteEntity();
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			selection.reset();
		}

		ImGui::End();
	}
}

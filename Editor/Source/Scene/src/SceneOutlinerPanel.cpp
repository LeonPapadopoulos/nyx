#include "SceneOutlinerPanel.h"

#include "EntityDragDrop.h"
#include "ImGuiSource.h"
#include "NameComponent.h"
#include "SceneEditor.h"
#include "UIWidgets.h"

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

		if (NYX_UI(Nyx::UI::Button("Add Entity")))
		{
			editing.CreateEntity();
		}

		ImGui::SameLine();

		const bool bHasSelection = selection.has_value();
		if (!bHasSelection)
		{
			ImGui::BeginDisabled();
		}

		if (NYX_UI(Nyx::UI::Button("Delete Selected")) && bHasSelection)
		{
			editing.DeleteSelectedEntity();
		}

		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Duplicate")) && bHasSelection)
		{
			editing.DuplicateSelectedEntity();
		}
		ImGui::SetItemTooltip("Ctrl+D. Ctrl+C and Ctrl+V copy and paste; right-click an entity for these too.");

		if (!bHasSelection)
		{
			ImGui::EndDisabled();
		}

		NYX_UI(Nyx::UI::Separator());

		world.Each<Nyx::NameComponent>(
			[&](Nyx::Entity entity, const Nyx::NameComponent& name)
			{
				Nyx::UI::TreeRowOptions row;
				row.bSelected = selection.has_value() && selection.value() == entity;
				// No hierarchy yet: every entity is a leaf
				row.bLeaf = true;
				row.Icon = Nyx::UI::ETreeRowIcon::Entity;

				std::string label = name.Name + "##" + std::to_string(entity.Index());

				if (NYX_UI(Nyx::UI::TreeRow(label.c_str(), row)).bClicked)
				{
					// A mouse press selects once the button is up without a drag, so the entity can be
					// dragged onto a field of the selected entity's details; the keyboard selects at once
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						PressedEntity = entity;
					}
					else
					{
						selection = entity;
					}
				}

				// Right-click: selects the entity and offers what the hotkeys do
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					selection = entity;
					ImGui::OpenPopup("##EntityMenu");
				}

				// An entity can be dragged onto a field that names one, e.g. a surface anchor's host
				const EntityGuid guid = editing.Scene.GetGuid(entity);
				if (guid.IsValid() && ImGui::BeginDragDropSource())
				{
					SetEntityDragPayload(guid, name.Name);
					ImGui::EndDragDropSource();
				}
			});

		// A press that became a drag selects nothing
		if (PressedEntity && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			PressedEntity.reset();
		}
		if (PressedEntity && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (world.IsAlive(*PressedEntity))
			{
				selection = *PressedEntity;
			}
			PressedEntity.reset();
		}

		// After the list, so a new entity doesn't appear while the list is being drawn
		if (Nyx::UI::BeginPopup("##EntityMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Duplicate", "Ctrl+D")))
			{
				editing.DuplicateSelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Copy", "Ctrl+C")))
			{
				editing.CopySelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Paste", "Ctrl+V", false, editing.CanPaste())))
			{
				editing.PasteEntity();
			}
			NYX_UI(Nyx::UI::Separator());
			// As saved scenes, logs and NyxDump show it, e.g. 0x5F3A9C2D1E4B7A10
			const EntityGuid guid = selection ? editing.Scene.GetGuid(*selection) : EntityGuid{};
			if (NYX_UI(Nyx::UI::MenuItem("Copy Guid", nullptr, false, guid.IsValid())))
			{
				ImGui::SetClipboardText(GuidToText(guid).c_str());
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

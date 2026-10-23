#include "DetailsPanel.h"

#include "ComponentEdits.h"
#include "ComponentTypeRegistry.h"
#include "ImGuiSource.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionSourceRegistry.h"
#include "RuntimeMeshComponent.h"
#include "SceneEditor.h"
#include "TransactionObjectRefHelpers.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <cfloat>
#include <optional>
#include <string>
#include <utility>

namespace Nyx::Editor
{
	std::vector<std::string> DescribeRuntimeMesh(const Nyx::RuntimeMeshComponent& component)
	{
		if (!component.Mesh)
		{
			return { "No mesh yet" };
		}
		const Nyx::MeshData& mesh = *component.Mesh;
		std::vector<std::string> lines = { std::to_string(mesh.Vertices.size()) + " vertices, " + std::to_string(mesh.Indices.size() / 3) + " triangles" };
		const std::vector<Nyx::MeshSection> sections = Nyx::GetMeshSections(mesh);
		for (size_t index = 0; index < sections.size(); ++index)
		{
			lines.push_back("Section " + std::to_string(index) + ": " + std::to_string(sections[index].IndexCount / 3) + " triangles, material slot " +
				std::to_string(sections[index].MaterialSlot));
		}
		if (const std::string problem = Nyx::CheckMeshData(mesh); !problem.empty())
		{
			lines.push_back("Not drawn: " + problem);
		}
		lines.push_back("Revision " + std::to_string(component.Revision));
		return lines;
	}

	void DetailsPanel::Draw(SceneEditor& editing)
	{
		InspectorDrawContext& drawContext = editing.DetailsContext;

		// An entity reference picks only while it is drawn: closing its component or the panel, or
		// selecting another entity, stops the eyedropper
		drawContext.EntityPick.bDrawn = false;
		DrawWindow(editing);
		if (!drawContext.EntityPick.bDrawn)
		{
			drawContext.EntityPick = {};
		}

		// An entity reference's Select button, after drawing, so no row is drawn for another entity
		if (const std::optional<Nyx::Entity> toSelect = std::exchange(drawContext.EntityToSelect, std::nullopt))
		{
			if (editing.Scene.GetRegistry().IsAlive(*toSelect))
			{
				editing.Scene.GetSelection() = *toSelect;
			}
		}
	}

	void DetailsPanel::DrawWindow(SceneEditor& editing)
	{
		if (!bOpen)
		{
			return;
		}

		if (!Nyx::UI::Begin("Details", &bOpen))
		{
			ImGui::End();
			return;
		}

		auto& selection = editing.Scene.GetSelection();
		auto& world = editing.Scene.GetRegistry();

		if (!selection.has_value())
		{
			NYX_UI(ImGui::TextUnformatted("No entity selected."));
			ImGui::End();
			return;
		}

		const Nyx::Entity selectedEntity = selection.value();

		if (!world.IsAlive(selectedEntity))
		{
			NYX_UI(ImGui::TextUnformatted("Selected entity is no longer valid."));
			selection.reset();
			ImGui::End();
			return;
		}

		NYX_UI(ImGui::Text("Entity: %u", selectedEntity.Index()));
		NYX_UI(Nyx::UI::Separator());

		// @todo: Move away from manually hardcoding the visuals of
		// specific components (and fields) here; Consider DetailsPanel-,
		// and Property-Customizations like Unreal does it. Also, look
		// into code generation for the needed field meta data

		// A replacement scene can reuse entity handles. Give its widgets fresh IDs so ImGui
		// cannot carry an active drag or a cached text edit over from the previous scene.
		ImGui::PushID(std::to_string(editing.SceneRevision).c_str());
		ImGui::PushID(static_cast<int>(selectedEntity.Value));

		// @todo: Find a more robust (and automated) naming approach,
		// so we can easily, and reliably avoid Naming collisions among UI elements.
		// (Currently being dodged by using '##SomeSubInfo')

		InspectorDrawContext& drawContext = editing.DetailsContext;
		drawContext.Transactions = &editing.Transactions;
		drawContext.Entities = &world;
		drawContext.CurrentObjectRef = MakeSceneEntityRef(editing.Scene, selectedEntity);

		// Removed after the loop, so it isn't drawn after it is gone
		const Nyx::ComponentTypeOps* componentToRemove = nullptr;

		// One collapsible section per component of the entity, showing its reflected properties
		for (const Nyx::ComponentTypeOps& componentType : Nyx::ComponentTypeRegistry::Get().GetAll())
		{
			void* component = componentType.Get(world, selectedEntity);
			if (!component)
			{
				continue;
			}

			const char* displayName = componentType.TypeMetadata->DisplayName;
			ImGui::PushID(displayName);
			UI::SourceDeclarationScope componentSource(ReflectionSourceRegistry::Get().Find(*componentType.TypeMetadata));

			const bool bComponentOpen = NYX_UI(Nyx::UI::CollapsingHeader(displayName));

			// Right-click on the header: Remove, except for the guid and name every entity keeps
			if (CanAddOrRemoveComponent(componentType))
			{
				ImGui::SetItemTooltip("Right-click to remove");
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					ImGui::OpenPopup("##ComponentMenu");
				}

				if (Nyx::UI::BeginPopup("##ComponentMenu"))
				{
					if (NYX_UI(Nyx::UI::MenuItem("Remove Component")))
					{
						componentToRemove = &componentType;
					}
					ImGui::EndPopup();
				}
			}

			if (bComponentOpen)
			{
				// The component's own properties; the drawer extends the location for structs inside it
				drawContext.CurrentLocation = {};
				DrawReflectedTypeTable(component, *componentType.TypeMetadata, drawContext);
			}

			ImGui::PopID();
		}

		// Made by code while the world runs, so there is nothing to edit or save: what it is
		if (world.Has<Nyx::RuntimeMeshComponent>(selectedEntity))
		{
			ImGui::PushID("##RuntimeMesh");
			const bool bMeshOpen = NYX_UI(Nyx::UI::CollapsingHeader("Mesh Made at Run Time"));
			ImGui::SetItemTooltip("Made by code while the world runs, e.g. by a generator; not saved, made again after loading");
			if (bMeshOpen)
			{
				for (const std::string& line : DescribeRuntimeMesh(world.Get<Nyx::RuntimeMeshComponent>(selectedEntity)))
				{
					NYX_UI(ImGui::TextDisabled("%s", line.c_str()));
				}
			}
			ImGui::PopID();
		}

		// The component types the entity doesn't have yet
		NYX_UI(ImGui::Spacing());
		if (NYX_UI(Nyx::UI::Button("Add Component", glm::vec2(-FLT_MIN, 0.0f))))
		{
			ImGui::OpenPopup("##AddComponent");
		}

		const Nyx::ComponentTypeOps* componentToAdd = nullptr;
		if (Nyx::UI::BeginPopup("##AddComponent"))
		{
			bool bAnyMissing = false;
			for (const Nyx::ComponentTypeOps& componentType : Nyx::ComponentTypeRegistry::Get().GetAll())
			{
				if (!CanAddOrRemoveComponent(componentType) || componentType.Has(world, selectedEntity))
				{
					continue;
				}

				bAnyMissing = true;
				if (NYX_UI(Nyx::UI::MenuItem(componentType.TypeMetadata->DisplayName)))
				{
					componentToAdd = &componentType;
				}
			}

			if (!bAnyMissing)
			{
				NYX_UI(ImGui::TextDisabled("The entity has every component"));
			}
			ImGui::EndPopup();
		}

		// Adding or removing can move other components of that type in memory, so edits in progress,
		// which point at components, are dropped first. A click on a menu has ended them already.
		if (componentToAdd || componentToRemove)
		{
			editing.CancelPendingEdits();
		}

		if (componentToAdd)
		{
			AddComponent(editing.Scene, editing.Transactions, selectedEntity, *componentToAdd);
		}

		if (componentToRemove)
		{
			RemoveComponent(editing.Scene, editing.Transactions, selectedEntity, *componentToRemove);
		}

		ImGui::PopID();
		ImGui::PopID();
		ImGui::End();
	}
}

#include "DetailsPanel.h"

#include "BinaryArchive.h"
#include "ComponentEdits.h"
#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "ImGuiSource.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionSourceRegistry.h"
#include "SdfTerrainComponent.h"
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
	std::optional<RuntimeSection> GetRuntimeSection(const Nyx::Registry& world, Nyx::Entity entity, const std::optional<GameInspection>& game)
	{
		if (!game)
		{
			std::vector<Nyx::InspectedComponent> components = Nyx::InspectRuntimeComponents(world, entity);
			if (components.empty())
			{
				return std::nullopt;
			}
			return RuntimeSection{ "This world", std::move(components) };
		}

		if (!game->Answer)
		{
			return RuntimeSection{ "Waiting for " + game->GameName, {} };
		}
		if (!game->Answer->bFound)
		{
			return RuntimeSection{ game->GameName + " has no such entity", {} };
		}
		return RuntimeSection{ game->GameName + ", step " + std::to_string(game->Answer->Step), game->Answer->Components };
	}

	void DetailsPanel::Draw(SceneEditor& editing)
	{
		// Set again if the Runtime section is drawn open
		InspectedEntity.reset();

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

		// One collapsible section per component of the entity, showing its reflected properties; the
		// run-time ones after them, in the Runtime section
		for (const Nyx::ComponentTypeOps& componentType : Nyx::ComponentTypeRegistry::Get().GetAll())
		{
			void* component = componentType.IsRuntime() ? nullptr : componentType.Get(world, selectedEntity);
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

		// An SDF terrain's brush, after its components
		if (world.Has<Nyx::SdfTerrainComponent>(selectedEntity))
		{
			editing.TerrainBrush.DrawDetails();
		}

		DrawRuntimeSection(world, selectedEntity);

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

	void DetailsPanel::DrawRuntimeSection(const Nyx::Registry& world, Nyx::Entity entity)
	{
		const std::optional<RuntimeSection> section = GetRuntimeSection(world, entity, Game);
		if (!section)
		{
			return;
		}

		ImGui::PushID("##Runtime");
		const bool bOpen = NYX_UI(Nyx::UI::CollapsingHeader("Runtime"));
		ImGui::SetItemTooltip("Made while the world runs and never saved; while Play runs, as the game has it, a few times a second");
		if (bOpen)
		{
			if (world.Has<Nyx::GuidComponent>(entity))
			{
				InspectedEntity = world.Get<Nyx::GuidComponent>(entity).Guid;
			}

			NYX_UI(ImGui::TextDisabled("%s", section->Source.c_str()));
			for (const Nyx::InspectedComponent& component : section->Components)
			{
				DrawInspectedComponent(component);
			}
		}
		ImGui::PopID();
	}

	void DetailsPanel::DrawInspectedComponent(const Nyx::InspectedComponent& component)
	{
		ImGui::PushID(component.Type.c_str());
		const std::string& name = component.DisplayName.empty() ? component.Type : component.DisplayName;
		Nyx::UI::TreeRowOptions options;
		options.bDefaultOpen = true;
		options.bToggleOnClick = true;
		if (NYX_UI(Nyx::UI::TreeRow(name.c_str(), options)).bOpen)
		{
			// The properties read into a component of the type, if this editor knows it, and drawn as
			// any component's: greyed, and with nothing to undo
			const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByName(component.Type);
			if (!component.Properties.empty() && ops && ops->TypeMetadata->PropertyCount > 0)
			{
				if (!ScratchEntity || !Scratch.IsAlive(*ScratchEntity))
				{
					ScratchEntity = Scratch.CreateEntity();
				}
				ops->Remove(Scratch, *ScratchEntity);
				void* object = ops->Add(Scratch, *ScratchEntity);

				Nyx::BinaryReader reader;
				reader.LoadFromMemory(component.Properties);
				Nyx::ReadWarnings warnings;
				if (Nyx::ReflectedArchiveSerializer::DeserializeObject(reader, object, *ops->TypeMetadata, warnings))
				{
					ScratchContext.CurrentLocation = {};
					ImGui::BeginDisabled();
					DrawReflectedTypeTable(object, *ops->TypeMetadata, ScratchContext);
					ImGui::EndDisabled();
				}
				else
				{
					NYX_UI(ImGui::TextDisabled("Its properties can't be read"));
				}
			}
			else if (!component.Properties.empty())
			{
				NYX_UI(ImGui::TextDisabled("A type this editor doesn't know"));
			}

			for (const Nyx::InspectedValue& value : component.Values)
			{
				Nyx::UI::BeginPropertyRow(value.Name.c_str());
				NYX_UI(ImGui::TextDisabled("%s", value.Value.c_str()));
				Nyx::UI::EndPropertyRow();
			}
			Nyx::UI::TreeRowEnd();
		}
		ImGui::PopID();
	}
}

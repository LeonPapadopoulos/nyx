#include "DetailsPanel.h"

#include "BinaryArchive.h"
#include "ComponentEdits.h"
#include "ComponentTypeRegistry.h"
#include "EntityReferenceWidget.h"
#include "GuidComponent.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "ParentComponent.h"
#include "Prefab.h"
#include "PrefabEdits.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionSourceRegistry.h"
#include "SdfTerrainComponent.h"
#include "SceneEditor.h"
#include "SearchRow.h"
#include "TransactionObjectRefHelpers.h"
#include "TransformComponent.h"
#include "TransformParents.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <optional>
#include <string>
#include <utility>

namespace Nyx::Editor
{
	namespace
	{
		const Nyx::Reflection::PropertyMetadata& GetParentProperty()
		{
			const Nyx::Reflection::TypeMetadata& linkType = Nyx::Reflection::GetTypeMetadata<Nyx::ParentComponent>();
			for (size_t index = 0; index < linkType.PropertyCount; ++index)
			{
				if (std::string_view(linkType.Properties[index].Name) == "Parent")
				{
					return linkType.Properties[index];
				}
			}
			return linkType.Properties[0];
		}

		// A row of text in the colour of warnings, under the property it is about
		void DrawWarningRow(const char* text)
		{
			Nyx::UI::BeginPropertyRow("##Warning");
			const glm::vec4& warning = Nyx::UI::GetTheme().StatusWarning;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(warning.r, warning.g, warning.b, warning.a));
			NYX_UI(ImGui::TextWrapped("%s", text));
			ImGui::PopStyleColor();
			Nyx::UI::EndPropertyRow();
		}

		// The Transform section: the entity's parent, then for a child its values relative to the
		// parent, editable, and its place in the world, greyed; for an entity without a parent its
		// transform as any component's properties. Returns the parent chosen this frame (none for
		// None), to be set once the panel is drawn, as it adds or removes the link.
		std::optional<std::optional<Nyx::Entity>> DrawTransformSection(Nyx::Registry& world, Nyx::Entity entity,
			Nyx::TransformComponent& transform, InspectorDrawContext& drawContext)
		{
			std::optional<std::optional<Nyx::Entity>> chosenParent;

			Nyx::ParentComponent* link = world.Has<Nyx::ParentComponent>(entity) ? &world.Get<Nyx::ParentComponent>(entity) : nullptr;
			const Nyx::Reflection::PropertyMetadata& parentProperty = GetParentProperty();

			if (NameMatchesFilter("Parent", drawContext.PropertyFilter))
			{
				ImGui::PushID("##Parent");
				Nyx::UI::BeginPropertyRow("Parent##Row");
				ImGui::SetItemTooltip("The entity this one moves, turns and scales with; changing it keeps this one where it is");
				if (const std::optional<Nyx::EntityGuid> chosen =
						DrawEntityReferenceField(drawContext, link ? link->Parent : Nyx::EntityGuid{}, parentProperty, false))
				{
					chosenParent = chosen->IsValid() ? Nyx::FindEntityByGuid(world, *chosen) : std::nullopt;
				}
				Nyx::UI::EndPropertyRow();
				if (link && link->Parent.IsValid() && !FindParent(world, entity))
				{
					DrawWarningRow("The parent isn't in the scene: this entity stays where it is");
				}
				else if (link && link->bFollowInGame && IsMovedByPhysicsInGame(world, entity))
				{
					DrawWarningRow("Physics moves this entity's body in the game, so it won't follow: join it to its parent with a constraint, or make its Rigid Body kinematic");
				}
				else if (link && HasParentMovingInGame(world, entity) && !FollowsMovingParentInGame(world, entity))
				{
					DrawWarningRow("A parent moves in the game (a rigid body or a character): tick Follows in Game, here and on each entity between, for this one to come along");
				}
				ImGui::PopID();
			}

			const Nyx::Reflection::TypeMetadata& transformType = Nyx::Reflection::GetTypeMetadata<Nyx::TransformComponent>();
			if (!link)
			{
				DrawReflectedTypeTable(&transform, transformType, drawContext);
				return chosenParent;
			}

			// Relative to the parent: what is saved and undone
			DrawReflectedTypeTable(link, Nyx::Reflection::GetTypeMetadata<Nyx::ParentComponent>(), drawContext);

			// In the world: worked out from the parent, so not edited here
			const bool bSearching = !drawContext.PropertyFilter.empty();
			if (bSearching && !ReflectedTypeHasMatch(transformType, drawContext.PropertyFilter))
			{
				return chosenParent;
			}
			ImGui::PushID("##World");
			Nyx::UI::TreeRowOptions options;
			options.bToggleOnClick = true;
			if (bSearching)
			{
				ImGui::SetNextItemOpen(true);
			}
			if (NYX_UI(Nyx::UI::TreeRow("In the World", options)).bOpen)
			{
				ImGui::BeginDisabled();
				for (size_t index = 0; index < transformType.PropertyCount; ++index)
				{
					DrawReflectedProperty(&transform, transformType, index, drawContext);
				}
				ImGui::EndDisabled();
				Nyx::UI::TreeRowEnd();
			}
			ImGui::PopID();

			return chosenParent;
		}
	}

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
		ScratchContext.PropertyFilter = {};

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
				editing.Scene.Select(*toSelect);
			}
		}
		drawContext.PropertyFilter = {};
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

		const auto& selection = editing.Scene.GetSelection();
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
			editing.Scene.Select(std::nullopt);
			ImGui::End();
			return;
		}

		NYX_UI(ImGui::Text("Entity: %u", selectedEntity.Index()));
		if (const size_t selectedCount = editing.Scene.GetSelectedEntities().size(); selectedCount > 1)
		{
			NYX_UI(ImGui::TextDisabled("%zu selected; these are the details of the last", selectedCount));
		}

		// Which prefab it follows, if it is an instance's root or part
		if (const std::optional<Nyx::Entity> instanceRoot = FindPrefabInstanceRoot(world, selectedEntity))
		{
			const std::string& prefabPath = world.Get<Nyx::PrefabInstanceComponent>(*instanceRoot).Prefab.Path;
			NYX_UI(ImGui::TextDisabled(*instanceRoot == selectedEntity ? "Instance of %s" : "Part of an instance of %s", prefabPath.c_str()));
			ImGui::SetItemTooltip("Values that differ from the prefab are marked at their left; right-click one, or a component's header, to revert it or apply it to the prefab");
		}

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
		{
			bFocusSearch = true;
		}
		DrawSearchRow(Filter, bFocusSearch, "Shows the components and properties whose names have this in them (Ctrl+F)");
		NYX_UI(Nyx::UI::Separator());

		// @todo: Move away from manually hardcoding the visuals of
		// specific components (and fields) here; Consider DetailsPanel-,
		// and Property-Customizations like Unreal does it. Also, look
		// into code generation for the needed field meta data

		// A replacement scene can reuse entity handles. Give its widgets fresh IDs so ImGui
		// cannot carry an active drag or a cached text edit over from the previous scene.
		ImGui::PushID(std::to_string(editing.SceneRevision).c_str());
		ImGui::PushID(static_cast<int>(selectedEntity.Value));

		// Open while searching, in an ID scope of its own, so that what was open shows again after it
		const bool bSearching = !Filter.empty();
		bool bAnyShown = false;
		ImGui::PushID(bSearching ? "##Searching" : "##All");

		// @todo: Find a more robust (and automated) naming approach,
		// so we can easily, and reliably avoid Naming collisions among UI elements.
		// (Currently being dodged by using '##SomeSubInfo')

		InspectorDrawContext& drawContext = editing.DetailsContext;
		drawContext.Transactions = &editing.Transactions;
		drawContext.Entities = &world;
		drawContext.CurrentObjectRef = MakeSceneEntityRef(editing.Scene, selectedEntity);

		// Removed after the loop, so it isn't drawn after it is gone
		const Nyx::ComponentTypeOps* componentToRemove = nullptr;

		// Set after the loop too: it adds or removes the parent link
		std::optional<std::optional<Nyx::Entity>> parentToSet;

		// A value, or a component, reverted to or applied to the prefab, after the loop: making the
		// instances again moves their components
		struct PrefabAction
		{
			const Nyx::ComponentTypeOps* Component = nullptr;
			std::optional<size_t> Property;
			bool bApply = false;
		};
		std::optional<PrefabAction> prefabAction;
		const Nyx::Reflection::TypeMetadata& transformType = Nyx::Reflection::GetTypeMetadata<Nyx::TransformComponent>();
		const Nyx::Reflection::TypeMetadata& linkType = Nyx::Reflection::GetTypeMetadata<Nyx::ParentComponent>();

		const auto hasMatch = [&](const Nyx::Reflection::TypeMetadata& type)
		{
			return ReflectedTypeHasMatch(type, Filter);
		};

		// One collapsible section per component of the entity, showing its reflected properties; the
		// run-time ones after them, in the Runtime section
		for (const Nyx::ComponentTypeOps& componentType : Nyx::ComponentTypeRegistry::Get().GetAll())
		{
			// The parent link is drawn inside the Transform section
			void* component = componentType.IsRuntime() || componentType.TypeMetadata == &linkType ? nullptr : componentType.Get(world, selectedEntity);
			if (!component)
			{
				continue;
			}

			// A search: a component with it in its name whole, another for the properties that have it
			const char* displayName = componentType.TypeMetadata->DisplayName;
			const bool bTransformSection = componentType.TypeMetadata == &transformType;
			const bool bWhole = NameMatchesFilter(displayName, Filter);
			if (!bWhole && !hasMatch(*componentType.TypeMetadata) &&
				!(bTransformSection && (NameMatchesFilter("Parent", Filter) || (world.Has<Nyx::ParentComponent>(selectedEntity) && hasMatch(linkType)))))
			{
				continue;
			}
			bAnyShown = true;
			drawContext.PropertyFilter = bWhole ? std::string_view{} : std::string_view(Filter);

			ImGui::PushID(displayName);
			UI::SourceDeclarationScope componentSource(ReflectionSourceRegistry::Get().Find(*componentType.TypeMetadata));

			// Of a prefab instance: what differs from the prefab, and whether the instance added it.
			// The parent link is drawn in the Transform section, its own values marked there.
			const std::vector<size_t> overridden = FindOverriddenProperties(editing, selectedEntity, componentType);
			const std::vector<size_t> linkOverridden = componentType.TypeMetadata == &transformType && world.Has<Nyx::ParentComponent>(selectedEntity)
				? FindOverriddenProperties(editing, selectedEntity, *Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(linkType))
				: std::vector<size_t>{};
			const bool bAdded = IsAddedToInstance(editing, selectedEntity, componentType);
			const std::string header = std::string(displayName) + (bAdded ? " (added to the instance)" : !overridden.empty() || !linkOverridden.empty() ? " (overridden)" : "") +
				"###" + displayName;
			if (bSearching)
			{
				ImGui::SetNextItemOpen(true);
			}
			const bool bComponentOpen = NYX_UI(Nyx::UI::CollapsingHeader(header.c_str()));

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
					if (!overridden.empty() || bAdded)
					{
						NYX_UI(Nyx::UI::Separator());
						if (NYX_UI(Nyx::UI::MenuItem("Revert to Prefab", nullptr, false, !overridden.empty())))
						{
							prefabAction = PrefabAction{ .Component = &componentType };
						}
						if (NYX_UI(Nyx::UI::MenuItem("Apply to Prefab")))
						{
							prefabAction = PrefabAction{ .Component = &componentType, .bApply = true };
						}
					}
					ImGui::EndPopup();
				}
			}

			if (bComponentOpen)
			{
				// The component's own properties; the drawer extends the location for structs inside it
				drawContext.CurrentLocation = {};
				const bool bTransform = componentType.TypeMetadata == &transformType;
				const Nyx::ComponentTypeOps* marked = bTransform ? Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(linkType) : &componentType;
				drawContext.PrefabOverrides = bTransform ? &linkOverridden : &overridden;
				drawContext.PrefabOverridesType = marked ? marked->TypeMetadata : nullptr;
				drawContext.PrefabValueAction = [&prefabAction, marked](size_t propertyIndex, bool bApply)
				{
					prefabAction = PrefabAction{ .Component = marked, .Property = propertyIndex, .bApply = bApply };
				};
				if (bTransform)
				{
					parentToSet = DrawTransformSection(world, selectedEntity, *static_cast<Nyx::TransformComponent*>(component), drawContext);
				}
				else
				{
					DrawReflectedTypeTable(component, *componentType.TypeMetadata, drawContext);
				}
				drawContext.PrefabOverrides = nullptr;
				drawContext.PrefabOverridesType = nullptr;
				drawContext.PrefabValueAction = nullptr;
			}

			ImGui::PopID();
		}

		drawContext.PropertyFilter = {};

		// An SDF terrain's brush, after its components
		if (world.Has<Nyx::SdfTerrainComponent>(selectedEntity))
		{
			bAnyShown |= editing.TerrainBrush.DrawDetails(Filter);
		}

		bAnyShown |= DrawRuntimeSection(world, selectedEntity);

		if (bSearching && !bAnyShown)
		{
			NYX_UI(ImGui::TextDisabled("No component or property here has \"%s\"", Filter.c_str()));
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
		if (componentToAdd || componentToRemove || parentToSet || prefabAction)
		{
			editing.CancelPendingEdits();
		}

		if (prefabAction && prefabAction->Component)
		{
			if (prefabAction->bApply)
			{
				ApplyToPrefab(editing, selectedEntity, *prefabAction->Component, prefabAction->Property);
			}
			else
			{
				RevertToPrefab(editing, selectedEntity, *prefabAction->Component, prefabAction->Property);
			}
		}

		if (parentToSet && !SetParent(editing.Scene, editing.Transactions, selectedEntity, *parentToSet) && *parentToSet &&
			IsSelfOrBelow(world, **parentToSet, selectedEntity))
		{
			LOG_WARNING("An entity can't be placed below itself, or below an entity placed below it");
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
		ImGui::PopID();
		ImGui::End();
	}

	bool DetailsPanel::DrawRuntimeSection(const Nyx::Registry& world, Nyx::Entity entity)
	{
		const std::optional<RuntimeSection> section = GetRuntimeSection(world, entity, Game);
		if (!section)
		{
			return false;
		}

		// A search: the section with it in its name whole, else the components that have it
		const std::string_view filter = NameMatchesFilter("Runtime", Filter) ? std::string_view{} : std::string_view(Filter);
		const auto componentHasMatch = [&](const Nyx::InspectedComponent& component)
		{
			const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByName(component.Type);
			return NameMatchesFilter(component.DisplayName.empty() ? component.Type : component.DisplayName, filter) ||
				(ops && ReflectedTypeHasMatch(*ops->TypeMetadata, filter)) ||
				std::any_of(component.Values.begin(), component.Values.end(),
					[&](const Nyx::InspectedValue& value)
					{
						return NameMatchesFilter(value.Name, filter);
					});
		};
		if (!filter.empty() && std::none_of(section->Components.begin(), section->Components.end(), componentHasMatch))
		{
			return false;
		}

		ImGui::PushID("##Runtime");
		if (!Filter.empty())
		{
			ImGui::SetNextItemOpen(true);
		}
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
				if (filter.empty() || componentHasMatch(component))
				{
					DrawInspectedComponent(component, filter);
				}
			}
		}
		ImGui::PopID();
		return true;
	}

	void DetailsPanel::DrawInspectedComponent(const Nyx::InspectedComponent& component, std::string_view filter)
	{
		ImGui::PushID(component.Type.c_str());
		const std::string& name = component.DisplayName.empty() ? component.Type : component.DisplayName;
		if (NameMatchesFilter(name, filter))
		{
			filter = {};
		}
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
					ScratchContext.PropertyFilter = filter;
					ImGui::BeginDisabled();
					DrawReflectedTypeTable(object, *ops->TypeMetadata, ScratchContext);
					ImGui::EndDisabled();
					ScratchContext.PropertyFilter = {};
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
				if (!NameMatchesFilter(value.Name, filter))
				{
					continue;
				}
				Nyx::UI::BeginPropertyRow(value.Name.c_str());
				NYX_UI(ImGui::TextDisabled("%s", value.Value.c_str()));
				Nyx::UI::EndPropertyRow();
			}
			Nyx::UI::TreeRowEnd();
		}
		ImGui::PopID();
	}
}

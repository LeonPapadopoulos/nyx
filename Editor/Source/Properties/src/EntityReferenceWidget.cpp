#include "EntityReferenceWidget.h"

#include "EntityDragDrop.h"
#include "GuidComponent.h"
#include "ImGuiSource.h"
#include "NameComponent.h"
#include "ReflectionUtils.h"
#include "UIWidgets.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

namespace
{
	using namespace Nyx::Editor;

	static std::string GetEntityName(const Nyx::Registry& world, Nyx::Entity entity)
	{
		return world.Has<Nyx::NameComponent>(entity) ? world.Get<Nyx::NameComponent>(entity).Name : std::string("Entity");
	}

	static bool ContainsIgnoringCase(std::string_view text, std::string_view part)
	{
		const auto sameLetter = [](char a, char b)
		{
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		};
		return std::search(text.begin(), text.end(), part.begin(), part.end(), sameLetter) != text.end();
	}

	// The combo's list: None, then the scene's entities in the outliner's order, those whose name or
	// guid has the filter in it
	static std::optional<Nyx::EntityGuid> DrawEntityList(InspectorDrawContext& context, Nyx::EntityGuid current)
	{
		std::optional<Nyx::EntityGuid> chosen;

		if (ImGui::IsWindowAppearing())
		{
			context.EntityFilter.clear();
			ImGui::SetKeyboardFocusHere();
		}
		NYX_UI(Nyx::UI::SearchField("##Filter", &context.EntityFilter, "Filter"));

		if (NYX_UI(Nyx::UI::Selectable("None", !current.IsValid())))
		{
			chosen = Nyx::EntityGuid{};
		}

		if (!context.Entities)
		{
			return chosen;
		}

		const Nyx::Registry& world = *context.Entities;
		world.Each<Nyx::NameComponent>(
			[&](Nyx::Entity entity, const Nyx::NameComponent& name)
			{
				if (!world.Has<Nyx::GuidComponent>(entity))
				{
					return;
				}

				const Nyx::EntityGuid guid = world.Get<Nyx::GuidComponent>(entity).Guid;
				const std::string guidText = Nyx::GuidToText(guid);
				if (!guid.IsValid() ||
					(!ContainsIgnoringCase(name.Name, context.EntityFilter) && !ContainsIgnoringCase(guidText, context.EntityFilter)))
				{
					return;
				}

				// Two entities may share a name; their guids keep the items apart
				const std::string label = name.Name + "##" + guidText;
				if (NYX_UI(Nyx::UI::Selectable(label.c_str(), guid == current)))
				{
					chosen = guid;
				}
			});

		return chosen;
	}
}

namespace Nyx::Editor
{
	std::string DescribeEntityReference(const Nyx::Registry* world, Nyx::EntityGuid guid)
	{
		if (!guid.IsValid())
		{
			return "None";
		}
		if (const std::optional<Nyx::Entity> entity = world ? Nyx::FindEntityByGuid(*world, guid) : std::nullopt)
		{
			return GetEntityName(*world, *entity);
		}
		return "Missing (" + Nyx::GuidToText(guid) + ")";
	}

	bool DrawEntityReferenceWidget(const PropertyWidgetArgs& args)
	{
		InspectorDrawContext& context = *args.DrawContext;
		uint64_t& value = Nyx::Reflection::AccessProperty<uint64_t>(args.Object, *args.Property);
		const Nyx::EntityGuid current{ value };
		const std::optional<Nyx::Entity> entity =
			context.Entities && current.IsValid() ? Nyx::FindEntityByGuid(*context.Entities, current) : std::nullopt;
		const bool bReadOnly = Nyx::Reflection::HasFlag(args.Property->Flags, Nyx::Reflection::EPropertyFlags::ReadOnly);

		std::optional<Nyx::EntityGuid> chosen;

		// The eyedropper, when this reference is the one picking: takes the entity a scene view
		// picked, or stops on Esc
		EntityPickState& pick = context.EntityPick;
		bool bPicking = pick.Property == args.Property && pick.Target == context.CurrentObjectRef &&
			pick.Location == context.CurrentLocation;
		if (bPicking)
		{
			pick.bDrawn = true;
			if (pick.Picked)
			{
				chosen = pick.Picked;
				pick = {};
				bPicking = false;
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				pick = {};
				bPicking = false;
			}
		}

		// The buttons at the right end of the row first, so the combo is the row's last item and the
		// property's tooltip goes with it
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float buttonSize = ImGui::GetFrameHeight();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float comboWidth = std::max(1.0f, ImGui::CalcItemWidth() - 2.0f * (buttonSize + spacing));

		ImGui::SetCursorScreenPos(ImVec2(start.x + comboWidth + spacing, start.y));
		const Nyx::UI::EButtonTone pickTone = bPicking ? Nyx::UI::EButtonTone::Primary : Nyx::UI::EButtonTone::Neutral;
		if (NYX_UI(Nyx::UI::IconButton("##Pick", Nyx::UI::EIcon::Eyedropper, glm::vec2(buttonSize), pickTone)))
		{
			pick = {};
			if (!bPicking)
			{
				pick.Target = context.CurrentObjectRef;
				pick.Location = context.CurrentLocation;
				pick.Property = args.Property;
				pick.bDrawn = true;
			}
		}
		ImGui::SetItemTooltip(bPicking ? "Click an entity in a scene view to pick it; Esc stops" : "Pick the entity in a scene view");

		ImGui::SameLine(0.0f, spacing);
		ImGui::BeginDisabled(!entity.has_value());
		if (NYX_UI(Nyx::UI::IconButton("##Select", Nyx::UI::EIcon::Locate, glm::vec2(buttonSize))) && entity)
		{
			context.EntityToSelect = *entity;
		}
		ImGui::EndDisabled();
		const std::string description = DescribeEntityReference(context.Entities, current);
		if (entity)
		{
			ImGui::SetItemTooltip("Select %s", description.c_str());
		}

		ImGui::SetCursorScreenPos(start);
		ImGui::SetNextItemWidth(comboWidth);
		if (NYX_UI(Nyx::UI::BeginCombo("##Entity", description.c_str())))
		{
			if (std::optional<Nyx::EntityGuid> listed = DrawEntityList(context, current))
			{
				chosen = listed;
			}
			Nyx::UI::EndCombo();
		}
		if (!bReadOnly && ImGui::BeginDragDropTarget())
		{
			if (std::optional<Nyx::EntityGuid> dropped = AcceptEntityDrop())
			{
				chosen = dropped;
			}
			ImGui::EndDragDropTarget();
		}
		// The guid itself, which saved scenes, logs and NyxDump show
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", current.IsValid() ? Nyx::GuidToText(current).c_str() : "No entity: drop one from the outliner here");
		}

		if (!chosen || chosen->Value == value)
		{
			return false;
		}

		TransactionDiffUtil diff;
		diff.TakeSnapshot(context.CurrentObjectRef, args.Object, *args.OwnerType, context.CurrentLocation);
		value = chosen->Value;
		if (context.Transactions)
		{
			const char* displayName = (args.Property->DisplayName && args.Property->DisplayName[0]) ? args.Property->DisplayName : args.Property->Name;
			const std::string label = "Assign " + std::string(displayName);
			diff.CommitChanges(label.c_str(), *context.Transactions);
		}
		return true;
	}
}

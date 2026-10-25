#include "OverrideRows.h"

#include "ImGuiSource.h"
#include "ValueOverride.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
#include "TransactionDiffUtil.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace
{
	using namespace Nyx::Editor;

	std::string FormatValue(float value)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%g", value);
		return text;
	}

	const char* GetDisplayName(const Nyx::Reflection::PropertyMetadata& property)
	{
		return (property.DisplayName && property.DisplayName[0]) ? property.DisplayName : property.Name;
	}

	std::string GetDisplayName(const Nyx::OverridableValue& value)
	{
		return value.DisplayName.empty() ? value.Name : value.DisplayName;
	}

	// A row's tooltip: the value's own, where its inherited value comes from, and its range
	std::string DescribeValue(const Nyx::OverridableValue& value, bool bOverridden)
	{
		std::string text = value.Tooltip;
		const auto addLine = [&](const std::string& line)
		{
			text += (text.empty() ? "" : "\n") + line;
		};

		const std::string from = value.Source.empty() ? std::string() : " from " + value.Source;
		addLine(bOverridden ? "Overrides " + FormatValue(value.Inherited) + from + "; the arrow resets it"
							: "Inherited" + from + "; editing it overrides it");
		const bool bMin = value.Min > -FLT_MAX;
		const bool bMax = value.Max < FLT_MAX;
		if (bMin && bMax)
		{
			addLine("Kept in " + FormatValue(value.Min) + " to " + FormatValue(value.Max));
		}
		else if (bMin)
		{
			addLine("Kept at " + FormatValue(value.Min) + " or more");
		}
		else if (bMax)
		{
			addLine("Kept at " + FormatValue(value.Max) + " or less");
		}
		return text;
	}

	void DrawTooltipIfHovered(const std::string& text)
	{
		if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", text.c_str());
		}
	}

	// A two-hundredth of the range per pixel when it has both ends
	float GetDragSpeed(const Nyx::OverridableValue& value)
	{
		if (value.Min > -FLT_MAX && value.Max < FLT_MAX && value.Max > value.Min)
		{
			return (value.Max - value.Min) / 200.0f;
		}
		return 0.1f;
	}

	// The array as it is now, from `before`, as one change (ReplaceArrayChange)
	Transaction MakeArrayReplacement(const std::string& label, const InspectorDrawContext& drawContext, const void* component,
		const Nyx::Reflection::TypeMetadata& ownerType, size_t propertyIndex, std::vector<std::byte> before)
	{
		Transaction transaction;
		transaction.Label = label;
		transaction.Changes.push_back({ EChangeKind::ReplaceArray,
			ReplaceArrayChange{ .Target = drawContext.CurrentObjectRef,
				.TypeMetadata = &ownerType,
				.PropertyIndex = propertyIndex,
				.Location = drawContext.CurrentLocation,
				.Before = std::move(before),
				.After = SaveReflectedProperty(component, ownerType, propertyIndex) } });
		return transaction;
	}

	// Where an element of the array is, for undo, as the array's own rows record it
	SubobjectPath GetElementLocation(const InspectorDrawContext& drawContext, const Nyx::Reflection::TypeMetadata& ownerType, size_t propertyIndex, size_t index)
	{
		SubobjectPath location = drawContext.CurrentLocation;
		if (!location.SubobjectType)
		{
			location.SubobjectType = &ownerType;
		}
		location.PropertyIndices.push_back(propertyIndex);
		location.PropertyIndices.push_back(index);
		return location;
	}

	// The reset arrow or Remove button at the row's right end, before the value, so the value is the
	// row's last item and its tooltip goes with it (as an entity reference's buttons). Returns the
	// value's width; the cursor is back at the row's start.
	float DrawRowButton(const char* id, bool bReset, bool bShown, const char* tooltip, bool& outClicked)
	{
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const glm::vec2 size = bReset ? glm::vec2(ImGui::GetFrameHeight()) : Nyx::UI::CalcIconButtonSize(id, Nyx::UI::EIcon::None, false);
		const float valueWidth = std::max(1.0f, ImGui::CalcItemWidth() - size.x - spacing);
		if (bShown)
		{
			ImGui::SetCursorScreenPos(ImVec2(start.x + valueWidth + spacing, start.y));
			outClicked = NYX_UI(Nyx::UI::IconButton(id, bReset ? Nyx::UI::EIcon::Reset : Nyx::UI::EIcon::None, size));
			ImGui::SetItemTooltip("%s", tooltip);
			ImGui::SetCursorScreenPos(start);
		}
		return valueWidth;
	}
}

namespace Nyx::Editor
{
	const Nyx::ComponentTypeOps* FindOverridableValuesProvider(
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		const char* ui = Nyx::Reflection::FindMetadataValue(property, "UI");
		const Nyx::Reflection::ArrayMetadata* array = Nyx::Reflection::TryGetArray(property);
		if (!ui || std::string_view(ui) != "Overrides" || !array ||
			Nyx::Reflection::TryGetNestedType(array->Element) != &Nyx::Reflection::GetTypeMetadata<Nyx::ValueOverride>())
		{
			return nullptr;
		}

		const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(ownerType);
		return ops && ops->ListOverridableValues ? ops : nullptr;
	}

	bool DrawOverrideRows(
		void* component,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::TypeMetadata& ownerType,
		const Nyx::ComponentTypeOps& provider,
		InspectorDrawContext& drawContext)
	{
		std::vector<Nyx::ValueOverride>& overrides = Nyx::Reflection::AccessProperty<std::vector<Nyx::ValueOverride>>(component, property);
		const size_t propertyIndex = static_cast<size_t>(&property - ownerType.Properties);

		// Asked at every draw: another of the component's values may decide which there are
		std::vector<Nyx::OverridableValue> values;
		provider.ListOverridableValues(component, values);

		// Each value's override is the first of its name; the others are not listed
		std::vector<std::optional<size_t>> entries(values.size());
		std::vector<bool> bListed(overrides.size(), false);
		size_t overridden = 0;
		for (size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex)
		{
			for (size_t index = 0; index < overrides.size(); ++index)
			{
				if (overrides[index].Name == values[valueIndex].Name)
				{
					entries[valueIndex] = index;
					bListed[index] = true;
					++overridden;
					break;
				}
			}
		}
		std::vector<size_t> notListed;
		for (size_t index = 0; index < overrides.size(); ++index)
		{
			if (!bListed[index])
			{
				notListed.push_back(index);
			}
		}

		bool bChanged = false;
		ImGui::PushID(property.Name);

		std::string label = std::string(GetDisplayName(property)) + " (" + std::to_string(overridden) + " of " + std::to_string(values.size()) + " overridden";
		if (!notListed.empty())
		{
			label += ", " + std::to_string(notListed.size()) + " not listed";
		}
		label += ")###OverridesNode";
		Nyx::UI::TreeRowOptions options;
		options.bDefaultOpen = true;
		options.bToggleOnClick = true;
		const bool bOpen = NYX_UI(Nyx::UI::TreeRow(label.c_str(), options)).bOpen;
		if (const char* tooltip = Nyx::Reflection::FindMetadataValue(property, "Tooltip"))
		{
			DrawTooltipIfHovered(tooltip);
		}

		if (bOpen)
		{
			const bool bReadOnly = Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::ReadOnly);
			ImGui::BeginDisabled(bReadOnly);

			// Taken away after drawing, so no row is drawn after its override moved
			std::optional<size_t> toRemove;
			std::string removeLabel;

			OverrideEditState& edit = drawContext.OverrideEdit;
			for (size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex)
			{
				const Nyx::OverridableValue& value = values[valueIndex];
				std::optional<size_t> entry = entries[valueIndex];
				const std::string displayName = GetDisplayName(value);
				const std::string tooltip = DescribeValue(value, entry.has_value());

				ImGui::PushID(value.Name.c_str());
				Nyx::UI::BeginPropertyRow((displayName + "##Row").c_str());
				DrawTooltipIfHovered(tooltip);

				bool bReset = false;
				const std::string resetTooltip = "Reset to " + FormatValue(value.Inherited) + ", inherited";
				const float valueWidth = DrawRowButton("##Reset", true, entry.has_value(), resetTooltip.c_str(), bReset);
				if (bReset)
				{
					toRemove = entry;
					removeLabel = "Reset " + displayName;
				}

				// Inherited greyed, overridden in the colour of any value
				float shown = entry ? overrides[*entry].Value : value.Inherited;
				ImGui::SetNextItemWidth(valueWidth);
				if (!entry)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				}
				const bool bEdited = NYX_UI(Nyx::UI::DragFloat("##Value", &shown, GetDragSpeed(value), "%g"));
				if (!entry)
				{
					ImGui::PopStyleColor();
				}
				DrawTooltipIfHovered(tooltip);

				const auto isThisEdit = [&]()
				{
					return edit.bEditing && edit.Target == drawContext.CurrentObjectRef && edit.Property == &property && edit.Name == value.Name;
				};
				if (ImGui::IsItemActivated() && !isThisEdit())
				{
					edit = OverrideEditState{ .bEditing = true, .Target = drawContext.CurrentObjectRef, .Property = &property, .Name = value.Name };
				}

				if (bEdited && isThisEdit())
				{
					shown = value.Clamp(shown);
					if (!entry)
					{
						// The override made, and shown to a running game at once; the edit's end records
						// making it and its value as one undo step
						edit.ArrayBefore = SaveReflectedProperty(component, ownerType, propertyIndex);
						overrides.push_back(Nyx::ValueOverride{ .Name = value.Name, .Value = shown });
						entry = overrides.size() - 1;
						if (drawContext.Transactions && drawContext.CurrentObjectRef.IsValid())
						{
							drawContext.Transactions->Preview(MakeArrayReplacement("Edit Property", drawContext, component, ownerType, propertyIndex, *edit.ArrayBefore));
						}
					}
					if (!edit.PendingDiff)
					{
						// Its value then changes as an array element's does: previewed while dragged
						// (InspectorDrawContext::PreviewPendingEdits()), one undo step at the end
						edit.PendingDiff.emplace();
						edit.PendingDiff->TakeSnapshot(drawContext.CurrentObjectRef, &overrides[*entry],
							Nyx::Reflection::GetTypeMetadata<Nyx::ValueOverride>(), GetElementLocation(drawContext, ownerType, propertyIndex, *entry));
					}
					overrides[*entry].Value = shown;
					bChanged = true;
				}

				if (ImGui::IsItemDeactivated() && isThisEdit())
				{
					if (drawContext.Transactions && edit.ArrayBefore && drawContext.CurrentObjectRef.IsValid())
					{
						drawContext.Transactions->Push(
							MakeArrayReplacement("Override " + displayName, drawContext, component, ownerType, propertyIndex, std::move(*edit.ArrayBefore)));
					}
					else if (drawContext.Transactions && edit.PendingDiff && !edit.ArrayBefore)
					{
						edit.PendingDiff->CommitChanges("Edit Property", *drawContext.Transactions);
					}
					edit = {};
				}

				Nyx::UI::EndPropertyRow();
				ImGui::PopID();
			}

			// Overrides of values the provider doesn't list: kept, and shown, until removed
			const glm::vec4& warning = Nyx::UI::GetTheme().StatusWarning;
			for (const size_t index : notListed)
			{
				const Nyx::ValueOverride& entry = overrides[index];
				const bool bTwice = FindValueOverride(overrides, entry.Name) != &entry;
				const std::string tooltip = bTwice ? "A second override of " + entry.Name + ": only the first is used. Remove it."
												   : "Overridden, but no value of this name is listed now, e.g. it was renamed, or this instance "
													 "doesn't have it. It does nothing: remove it, or keep it for when it is listed again.";

				ImGui::PushID("##NotListed");
				ImGui::PushID(static_cast<int>(index));
				Nyx::UI::BeginPropertyRow((entry.Name + "##Row").c_str());
				DrawTooltipIfHovered(tooltip);

				bool bRemove = false;
				DrawRowButton("Remove", false, true, "Take this override away", bRemove);
				if (bRemove)
				{
					toRemove = index;
					removeLabel = "Remove Override";
				}

				ImGui::AlignTextToFramePadding();
				const std::string text = FormatValue(entry.Value) + (bTwice ? "  (overridden twice)" : "  (not listed)");
				NYX_UI(ImGui::TextColored(ImVec4(warning.r, warning.g, warning.b, warning.a), "%s", text.c_str()));
				DrawTooltipIfHovered(tooltip);

				Nyx::UI::EndPropertyRow();
				ImGui::PopID();
				ImGui::PopID();
			}

			if (values.empty() && overrides.empty())
			{
				Nyx::UI::BeginPropertyRow("##Nothing");
				ImGui::AlignTextToFramePadding();
				NYX_UI(ImGui::TextDisabled("Nothing to override"));
				Nyx::UI::EndPropertyRow();
			}

			if (toRemove)
			{
				std::vector<std::byte> before = SaveReflectedProperty(component, ownerType, propertyIndex);
				overrides.erase(overrides.begin() + static_cast<std::ptrdiff_t>(*toRemove));
				if (drawContext.Transactions && drawContext.CurrentObjectRef.IsValid())
				{
					drawContext.Transactions->Push(MakeArrayReplacement(removeLabel, drawContext, component, ownerType, propertyIndex, std::move(before)));
				}
				bChanged = true;
			}

			ImGui::EndDisabled();
			Nyx::UI::TreeRowEnd();
		}

		ImGui::PopID();
		return bChanged;
	}
}

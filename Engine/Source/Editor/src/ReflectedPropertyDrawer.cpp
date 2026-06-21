#include "ReflectedPropertyDrawer.h"
#include "ReflectedDiffUtil.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstdlib>
#include <map>
#include <string_view>

namespace
{
	static float WrapDegrees180(float degrees)
	{
		while (degrees > 180.0f)
		{
			degrees -= 360.0f;
		}

		while (degrees < -180.0f)
		{
			degrees += 360.0f;
		}

		return degrees;
	}

	static glm::vec3 WrapEulerDegrees180(const glm::vec3& degrees)
	{
		return glm::vec3(
			WrapDegrees180(degrees.x),
			WrapDegrees180(degrees.y),
			WrapDegrees180(degrees.z)
		);
	}
}

namespace Nyx::Editor
{
	static float GetPropertyDragSpeed(const Nyx::Reflection::PropertyMetadata& property, float defaultValue = 0.1f)
	{
		if (const char* value = Nyx::Reflection::FindMetadataValue(property, "DragSpeed"))
		{
			const float parsed = std::strtof(value, nullptr);
			if (parsed > 0.0f)
			{
				return parsed;
			}
		}

		return defaultValue;
	}

	static bool PropertyUsesDegreesUI(const Nyx::Reflection::PropertyMetadata& property)
	{
		if (const char* value = Nyx::Reflection::FindMetadataValue(property, "UI"))
		{
			return std::string_view(value) == "Degrees";
		}

		return false;
	}

	static const char* GetPropertyTooltip(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::FindMetadataValue(property, "Tooltip");
	}

	static const char* GetPropertyCategory(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::FindMetadataValue(property, "Category");
	}

	static bool IsPropertyHidden(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Hidden);
	}

	static bool IsPropertyReadOnly(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::ReadOnly);
	}

	static void DrawPropertyTooltipIfHovered(const Nyx::Reflection::PropertyMetadata& property)
	{
		if (ImGui::IsItemHovered())
		{
			if (const char* tooltip = GetPropertyTooltip(property))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
		}
	}

	static void BeginPropertyEdit(
		PropertyEditTransactionState& state,
		Nyx::Editor::InspectorDrawContext& drawContext,
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!state.bEditing || state.Target != drawContext.CurrentObjectRef)
		{
			state.bEditing = true;
			state.Target = drawContext.CurrentObjectRef;
			state.PendingDiff.emplace();
			state.PendingDiff->TakeSnapshot(drawContext.CurrentObjectRef, object, typeMetadata);
		}
	}

	static void CommitPropertyEdit(
		PropertyEditTransactionState& state,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const char* label)
	{
		if (state.PendingDiff.has_value() && drawContext.Transactions)
		{
			state.PendingDiff->CommitChanges(label, *drawContext.Transactions);
		}

		state.PendingDiff.reset();
		state.bEditing = false;
	}

	struct PropertyBuckets
	{
		std::vector<size_t> Uncategorized;
		std::map<std::string, std::vector<size_t>> ByCategory;
	};

	static PropertyBuckets BuildPropertyBuckets(const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		PropertyBuckets buckets{};

		for (size_t propertyIndex = 0; propertyIndex < typeMetadata.PropertyCount; ++propertyIndex)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[propertyIndex];

			if (IsPropertyHidden(property))
			{
				continue;
			}

			if (const char* category = GetPropertyCategory(property))
			{
				if (category[0] != '\0')
				{
					buckets.ByCategory[category].push_back(propertyIndex);
					continue;
				}
			}

			buckets.Uncategorized.push_back(propertyIndex);
		}

		return buckets;
	}

	static bool DrawBoolProperty(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		bool& value = Nyx::Reflection::AccessByOffset<bool>(object, property.Offset);
		bool editedValue = value;

		if (ImGui::Checkbox("##Field", &editedValue))
		{
			PropertyEditTransactionState immediateEditState{};
			immediateEditState.Target = drawContext.CurrentObjectRef;
			immediateEditState.PendingDiff.emplace();
			immediateEditState.PendingDiff->TakeSnapshot(drawContext.CurrentObjectRef, object, typeMetadata);

			value = editedValue;
			bAnyChanged = true;

			if (drawContext.Transactions)
			{
				immediateEditState.PendingDiff->CommitChanges("Edit Property", *drawContext.Transactions);
			}
		}

		DrawPropertyTooltipIfHovered(property);
		return bAnyChanged;
	}

	static bool DrawFloatProperty(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		float& value = Nyx::Reflection::AccessByOffset<float>(object, property.Offset);
		const float dragSpeed = GetPropertyDragSpeed(property);

		if (ImGui::DragFloat("##Field", &value, dragSpeed))
		{
			bAnyChanged = true;
		}

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(drawContext.GenericPropertyEdit, drawContext, object, typeMetadata);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(drawContext.GenericPropertyEdit, drawContext, "Edit Property");
		}

		DrawPropertyTooltipIfHovered(property);
		return bAnyChanged;
	}

	static bool DrawVec3Property(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		glm::vec3& value = Nyx::Reflection::AccessByOffset<glm::vec3>(object, property.Offset);
		const float dragSpeed = GetPropertyDragSpeed(property);

		if (ImGui::DragFloat3("##Field", &value.x, dragSpeed))
		{
			bAnyChanged = true;
		}

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(drawContext.GenericPropertyEdit, drawContext, object, typeMetadata);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(drawContext.GenericPropertyEdit, drawContext, "Edit Property");
		}

		DrawPropertyTooltipIfHovered(property);
		return bAnyChanged;
	}

	static bool DrawQuatProperty(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		glm::quat& value = Nyx::Reflection::AccessByOffset<glm::quat>(object, property.Offset);

		if (PropertyUsesDegreesUI(property))
		{
			auto& rotState = drawContext.TransformRotationEdit;

			if (!rotState.bEditing || rotState.Target != drawContext.CurrentObjectRef)
			{
				rotState.Target = drawContext.CurrentObjectRef;
				rotState.CachedDegrees =
					WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
			}

			if (ImGui::DragFloat3("##Field", &rotState.CachedDegrees.x, 1.0f))
			{
				value = glm::normalize(glm::quat(glm::radians(rotState.CachedDegrees)));
				bAnyChanged = true;
			}

			if (ImGui::IsItemActivated())
			{
				BeginPropertyEdit(rotState, drawContext, object, typeMetadata);
				rotState.Target = drawContext.CurrentObjectRef;
			}

			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				CommitPropertyEdit(rotState, drawContext, "Edit Property");
				rotState.CachedDegrees =
					WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
			}
		}
		else
		{
			ImGui::TextDisabled("<quat unsupported>");
		}

		DrawPropertyTooltipIfHovered(property);
		return bAnyChanged;
	}

	static bool DrawStringProperty(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		std::string& value = Nyx::Reflection::AccessByOffset<std::string>(object, property.Offset);

		if (ImGui::InputText("##Field", &value))
		{
			bAnyChanged = true;
		}

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(drawContext.GenericPropertyEdit, drawContext, object, typeMetadata);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(drawContext.GenericPropertyEdit, drawContext, "Edit Property");
		}

		DrawPropertyTooltipIfHovered(property);
		return bAnyChanged;
	}

	static bool DrawSingleProperty(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		bool bAnyChanged = false;

		ImGui::PushID(property.Name);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(property.DisplayName && property.DisplayName[0] ? property.DisplayName : property.Name);

		ImGui::TableSetColumnIndex(1);

		const bool bReadOnly = IsPropertyReadOnly(property);
		if (bReadOnly)
		{
			ImGui::BeginDisabled();
		}

		switch (property.Kind)
		{
		case Nyx::Reflection::EPropertyKind::Bool:
			bAnyChanged |= DrawBoolProperty(object, property, drawContext, typeMetadata);
			break;

		case Nyx::Reflection::EPropertyKind::Float:
			bAnyChanged |= DrawFloatProperty(object, property, drawContext, typeMetadata);
			break;

		case Nyx::Reflection::EPropertyKind::Vec3:
			bAnyChanged |= DrawVec3Property(object, property, drawContext, typeMetadata);
			break;

		case Nyx::Reflection::EPropertyKind::Quat:
			bAnyChanged |= DrawQuatProperty(object, property, drawContext, typeMetadata);
			break;

		case Nyx::Reflection::EPropertyKind::String:
			bAnyChanged |= DrawStringProperty(object, property, drawContext, typeMetadata);
			break;

		default:
			ImGui::TextDisabled("<unsupported>");
			DrawPropertyTooltipIfHovered(property);
			break;
		}

		if (bReadOnly)
		{
			ImGui::EndDisabled();
		}

		ImGui::PopID();
		return bAnyChanged;
	}
}

bool Nyx::Editor::DrawReflectedTypeTable(
	void* object,
	const Nyx::Reflection::TypeMetadata& typeMetadata,
	Nyx::Editor::InspectorDrawContext& drawContext)
{
	bool bAnyChanged = false;

	const PropertyBuckets buckets = BuildPropertyBuckets(typeMetadata);

	if (ImGui::BeginTable("ReflectedProperties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Property");
		ImGui::TableSetupColumn("Value");

		for (size_t propertyIndex : buckets.Uncategorized)
		{
			bAnyChanged |= DrawSingleProperty(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
		}

		for (const auto& [category, indices] : buckets.ByCategory)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::SeparatorText(category.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("");

			for (size_t propertyIndex : indices)
			{
				bAnyChanged |= DrawSingleProperty(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
			}
		}

		ImGui::EndTable();
	}

	return bAnyChanged;
}
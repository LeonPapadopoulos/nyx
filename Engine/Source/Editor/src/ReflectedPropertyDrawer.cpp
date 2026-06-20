#include "ReflectedPropertyDrawer.h"
#include "ReflectedDiffUtil.h"
#include "ReflectedPropertyAccess.h"

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
	template<typename TObject>
	TObject& AccessByOffset(void* object, size_t offset)
	{
		return *reinterpret_cast<TObject*>(static_cast<unsigned char*>(object) + offset);
	}

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
}

namespace Nyx::Editor
{
	bool DrawReflectedTypeTable(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext)
	{
		bool bAnyChanged = false;

		auto BeginEdit = [&](PropertyEditTransactionState& state)
			{
				if (!state.bEditing || state.Target != drawContext.CurrentObjectRef)
				{
					state.bEditing = true;
					state.Target = drawContext.CurrentObjectRef;
					state.PendingDiff.emplace();
					state.PendingDiff->TakeSnapshot(drawContext.CurrentObjectRef, object, typeMetadata);
				}
			};

		auto CommitEdit = [&](PropertyEditTransactionState& state, const char* label)
			{
				if (state.PendingDiff.has_value() && drawContext.Transactions)
				{
					state.PendingDiff->CommitChanges(label, *drawContext.Transactions);
				}

				state.PendingDiff.reset();
				state.bEditing = false;
			};

		auto DrawOneProperty = [&](const Nyx::Reflection::PropertyMetadata& property)
			{
				if (IsPropertyHidden(property))
				{
					return;
				}

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
				{
					bool& value = AccessByOffset<bool>(object, property.Offset);
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
					break;
				}

				case Nyx::Reflection::EPropertyKind::Float:
				{
					float& value = AccessByOffset<float>(object, property.Offset);
					const float dragSpeed = GetPropertyDragSpeed(property);

					if (ImGui::DragFloat("##Field", &value, dragSpeed))
					{
						bAnyChanged = true;
					}

					if (ImGui::IsItemActivated())
					{
						BeginEdit(drawContext.GenericPropertyEdit);
					}

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						CommitEdit(drawContext.GenericPropertyEdit, "Edit Property");
					}

					DrawPropertyTooltipIfHovered(property);
					break;
				}

				case Nyx::Reflection::EPropertyKind::Vec3:
				{
					glm::vec3& value = AccessByOffset<glm::vec3>(object, property.Offset);
					const float dragSpeed = GetPropertyDragSpeed(property);

					if (ImGui::DragFloat3("##Field", &value.x, dragSpeed))
					{
						bAnyChanged = true;
					}

					if (ImGui::IsItemActivated())
					{
						BeginEdit(drawContext.GenericPropertyEdit);
					}

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						CommitEdit(drawContext.GenericPropertyEdit, "Edit Property");
					}

					DrawPropertyTooltipIfHovered(property);
					break;
				}

				case Nyx::Reflection::EPropertyKind::Quat:
				{
					glm::quat& value = AccessByOffset<glm::quat>(object, property.Offset);

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
							BeginEdit(rotState);
							rotState.Target = drawContext.CurrentObjectRef;
						}

						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							CommitEdit(rotState, "Edit Property");
							rotState.CachedDegrees =
								WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
						}
					}
					else
					{
						ImGui::TextDisabled("<quat unsupported>");
					}

					DrawPropertyTooltipIfHovered(property);
					break;
				}

				case Nyx::Reflection::EPropertyKind::String:
				{
					std::string& value = AccessByOffset<std::string>(object, property.Offset);

					if (ImGui::InputText("##Field", &value))
					{
						bAnyChanged = true;
					}

					if (ImGui::IsItemActivated())
					{
						BeginEdit(drawContext.GenericPropertyEdit);
					}

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						CommitEdit(drawContext.GenericPropertyEdit, "Edit Property");
					}

					DrawPropertyTooltipIfHovered(property);
					break;
				}

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
			};

		if (ImGui::BeginTable("ReflectedProperties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Property");
			ImGui::TableSetupColumn("Value");

			std::map<std::string, std::vector<size_t>> propertiesByCategory;
			std::vector<size_t> uncategorized;

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
						propertiesByCategory[category].push_back(propertyIndex);
						continue;
					}
				}

				uncategorized.push_back(propertyIndex);
			}

			for (size_t propertyIndex : uncategorized)
			{
				DrawOneProperty(typeMetadata.Properties[propertyIndex]);
			}

			for (const auto& [category, indices] : propertiesByCategory)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::SeparatorText(category.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted("");

				for (size_t propertyIndex : indices)
				{
					DrawOneProperty(typeMetadata.Properties[propertyIndex]);
				}
			}

			ImGui::EndTable();
		}

		return bAnyChanged;
	}
}
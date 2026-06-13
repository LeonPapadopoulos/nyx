#include "ReflectedPropertyDrawer.h"
#include "ReflectedDiffUtil.h"
#include "ReflectedPropertyAccess.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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
}

namespace Nyx::Editor
{
	bool DrawReflectedTypeTable(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext)
	{
		bool bAnyChanged = false;

		if (!object || !typeMetadata.Properties || typeMetadata.PropertyCount == 0)
		{
			return false;
		}

		auto BeginEdit = [&](PropertyEditTransactionState& state)
			{
				if (!state.bEditing || state.TargetId != drawContext.CurrentTargetId)
				{
					state.bEditing = true;
					state.TargetId = drawContext.CurrentTargetId;
					state.PendingDiff.emplace();
					state.PendingDiff->TakeSnapshot(drawContext.CurrentTargetId, object, typeMetadata);
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

		if (!ImGui::BeginTable("ReflectedProperties", 2, ImGuiTableFlags_SizingStretchProp))
		{
			return false;
		}

		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		for (size_t propertyIndex = 0; propertyIndex < typeMetadata.PropertyCount; ++propertyIndex)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[propertyIndex];

			ImGui::PushID(static_cast<int>(propertyIndex));

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(property.DisplayName && property.DisplayName[0] != '\0'
				? property.DisplayName
				: property.Name);

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);

			switch (property.Kind)
			{
			case Nyx::Reflection::EPropertyKind::Vec3:
			{
				glm::vec3& value = AccessByOffset<glm::vec3>(object, property.Offset);
				if (ImGui::DragFloat3("##Field", &value.x, property.DragSpeed > 0.0f ? property.DragSpeed : 0.1f))
				{
					bAnyChanged = true;
				}

				if (ImGui::IsItemActivated())
				{
					if (std::string_view(property.Name) == "Position")
					{
						BeginEdit(drawContext.TransformPositionEdit);
					}
					else if (std::string_view(property.Name) == "Scale")
					{
						BeginEdit(drawContext.TransformScaleEdit);
					}
				}

				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					if (std::string_view(property.Name) == "Position")
					{
						CommitEdit(drawContext.TransformPositionEdit, "Edit Transform Position");
					}
					else if (std::string_view(property.Name) == "Scale")
					{
						CommitEdit(drawContext.TransformScaleEdit, "Edit Transform Scale");
					}
				}
				break;
			}

			case Nyx::Reflection::EPropertyKind::Quat:
			{
				glm::quat& value = AccessByOffset<glm::quat>(object, property.Offset);

				if (property.bDisplayAsDegrees)
				{
					auto& rotState = drawContext.TransformRotationEdit;

					if (!rotState.bEditing || rotState.TargetId != drawContext.CurrentTargetId)
					{
						rotState.TargetId = drawContext.CurrentTargetId;
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
						rotState.TargetId = drawContext.CurrentTargetId;
					}

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						CommitEdit(rotState, "Edit Transform Rotation");
						rotState.CachedDegrees =
							WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
					}
				}
				else
				{
					ImGui::TextDisabled("<quat unsupported>");
				}
				break;
			}

			default:
				ImGui::TextDisabled("<unsupported>");
				break;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
		return bAnyChanged;
	}
}
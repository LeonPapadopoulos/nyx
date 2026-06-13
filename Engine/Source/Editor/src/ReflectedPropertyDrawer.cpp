#include "ReflectedPropertyDrawer.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstring>

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
			case Nyx::Reflection::EPropertyKind::Float:
			{
				float& value = AccessByOffset<float>(object, property.Offset);
				if (ImGui::DragFloat("##Field", &value, property.DragSpeed > 0.0f ? property.DragSpeed : 0.1f))
				{
					bAnyChanged = true;
				}
				break;
			}

			case Nyx::Reflection::EPropertyKind::Vec3:
			{
				glm::vec3& value = AccessByOffset<glm::vec3>(object, property.Offset);
				if (ImGui::DragFloat3("##Field", &value.x, property.DragSpeed > 0.0f ? property.DragSpeed : 0.1f))
				{
					bAnyChanged = true;
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
						rotState.bEditing = true;
						rotState.TargetId = drawContext.CurrentTargetId;
					}

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						rotState.bEditing = false;
						rotState.CachedDegrees =
							WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
					}
				}
				else
				{
					glm::vec4 quatValue(value.x, value.y, value.z, value.w);
					if (ImGui::DragFloat4("##Field", &quatValue.x, property.DragSpeed > 0.0f ? property.DragSpeed : 0.01f))
					{
						value = glm::normalize(glm::quat(quatValue.w, quatValue.x, quatValue.y, quatValue.z));
						bAnyChanged = true;
					}
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
#pragma once

#include "imgui.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	enum class EEditorPropertyKind : uint8_t
	{
		Bool,
		Float,
		Vec3,
		String
	};

	struct EditorPropertyDesc
	{
		const char* Name = "";
		EEditorPropertyKind Kind = EEditorPropertyKind::Float;
		size_t Offset = 0;
		float DragSpeed = 0.1f;
	};

	inline bool InputTextString(const char* label, std::string& value)
	{
		char buffer[256]{};

		const size_t copyLength = std::min(value.size(), sizeof(buffer) - 1);
		std::memcpy(buffer, value.data(), copyLength);
		buffer[copyLength] = '\0';

		if (ImGui::InputText(label, buffer, sizeof(buffer)))
		{
			value = buffer;
			return true;
		}

		return false;
	}

	inline void DrawPropertyRow(void* objectBase, const EditorPropertyDesc& property)
	{
		uint8_t* base = reinterpret_cast<uint8_t*>(objectBase);

		// Left column: visible label
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(property.Name);

		// Right column: actual widget, with hidden label
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);

		const std::string widgetLabel = "##Field";

		switch (property.Kind)
		{
		case EEditorPropertyKind::Bool:
		{
			bool* value = reinterpret_cast<bool*>(base + property.Offset);
			ImGui::Checkbox(widgetLabel.c_str(), value);
			break;
		}

		case EEditorPropertyKind::Float:
		{
			float* value = reinterpret_cast<float*>(base + property.Offset);
			ImGui::DragFloat(widgetLabel.c_str(), value, property.DragSpeed);
			break;
		}

		case EEditorPropertyKind::Vec3:
		{
			glm::vec3* value = reinterpret_cast<glm::vec3*>(base + property.Offset);
			float temp[3] = { value->x, value->y, value->z };

			if (ImGui::DragFloat3(widgetLabel.c_str(), temp, property.DragSpeed))
			{
				value->x = temp[0];
				value->y = temp[1];
				value->z = temp[2];
			}
			break;
		}

		case EEditorPropertyKind::String:
		{
			std::string* value = reinterpret_cast<std::string*>(base + property.Offset);
			InputTextString(widgetLabel.c_str(), *value);
			break;
		}

		default:
			break;
		}
	}

	inline void DrawProperties(void* objectBase, const std::vector<EditorPropertyDesc>& properties)
	{
		if (properties.empty())
		{
			return;
		}

		if (ImGui::BeginTable("Properties", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			for (const EditorPropertyDesc& property : properties)
			{
				ImGui::PushID(property.Name);
				DrawPropertyRow(objectBase, property);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}
}
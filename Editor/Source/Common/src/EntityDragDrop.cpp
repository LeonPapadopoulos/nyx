#include "EntityDragDrop.h"

#include <imgui.h>

#include <cstring>

namespace Nyx::Editor
{
	void SetEntityDragPayload(Nyx::EntityGuid guid, const std::string& name)
	{
		ImGui::SetDragDropPayload(EntityDragPayload, &guid.Value, sizeof(guid.Value));
		ImGui::TextUnformatted(name.c_str());
	}

	std::optional<Nyx::EntityGuid> AcceptEntityDrop()
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityDragPayload);
		if (!payload || !payload->IsDelivery() || payload->DataSize != sizeof(uint64_t))
		{
			return std::nullopt;
		}

		Nyx::EntityGuid guid;
		std::memcpy(&guid.Value, payload->Data, sizeof(guid.Value));
		return guid;
	}
}

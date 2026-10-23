#pragma once

#include "EntityGuid.h"

#include <optional>
#include <string>

namespace Nyx::Editor
{
	// Entities dragged from the outliner: the payload is the entity's guid, which names it whatever
	// slot it is in
	inline constexpr const char* EntityDragPayload = "NYX_ENTITY";

	// In a drag source (ImGui::BeginDragDropSource()): drags the entity, its name beside the mouse
	void SetEntityDragPayload(Nyx::EntityGuid guid, const std::string& name);

	// In a drop target (ImGui::BeginDragDropTarget()): the guid of the entity dropped this frame
	std::optional<Nyx::EntityGuid> AcceptEntityDrop();
}
